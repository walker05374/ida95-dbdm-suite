# Relatório Técnico — Correção Definitiva do DBDM Switcher para Emuladores/VMs Android 11

**Data:** 10/11/2026  
**Alvo:** `DBDM_Switcher_P_com.dbdm.switcher.debug_V_v1.0.apk` (`com.dbdm.switcher.debug`)  
**Jogo Alvo:** Dead by Daylight Mobile (`com.netease.dbdena`)  
**Ambiente Alvo:** Emuladores e Máquinas Virtuais Android 11 (API 30) com Root + Compatibilidade Android 12–16 (ABX)

---

## 1. Sintomas Observados no Emulador Android 11

1. Ao executar a troca de conta ou **Nova Conta** no DBDM Switcher e abrir o jogo (`com.netease.dbdena`), o seletor de servidor reiniciava no **Servidor 2 Offline** (comportamento decorrente do `pm clear com.netease.dbdena`).
2. Ao alternar para o servidor **Online** dentro do jogo, o cliente autenticava novamente na **mesma conta antiga** (`user_id` final `96ed`).
3. Ao retornar ao DBDM Switcher, a interface exibia o novo `SSAID` (final `f933`), porém acompanhado do `user_id` antigo (final `96ed`).

---

## 2. Diagnóstico de Causa Raiz (Android 11 vs Android 16)

### 2.1. Cache em Memória RAM do `SettingsProvider` (`system_server`) no Modo VM
- No Android 11 (API 30), os SSAIDs por aplicativo ficam em `/data/system/users/0/settings_ssaid.xml` em formato **XML texto puro**, gerenciados pela classe `SettingsProvider` (`SettingsState` / `mSettingsRegistry`) dentro do processo `system_server`.
- Como qualquer emulador ou VM ativa automaticamente `vmMode = true` (`Modo VM: ON (sem reboot)`) em `SsaidManager.isVirtualMachine()`, o `system_server` não era reiniciado após a gravação do novo `settings_ssaid.xml`.
- Enquanto o `system_server` está em execução, o `SettingsProvider` **nunca relê o arquivo `/data/system/users/0/settings_ssaid.xml` do disco**:
  - `SsaidManager.getCurrentSsaid()` lia diretamente o arquivo `/data/system/users/0/settings_ssaid.xml` no disco (ou o fallback `settings get secure android_id`) e exibia o novo valor (`...f933`).
  - Já o jogo (`com.netease.dbdena`, UID >= 10000) consulta `Settings.Secure.ANDROID_ID` via Binder IPC no `SettingsProvider`, que continuava respondendo com o **SSAID antigo retido na memória RAM do `system_server`**.
  - Ao conectar no servidor **Online** enviando o SSAID antigo da RAM, o servidor identificava o dispositivo antigo, devolvia o `user_id: ...96ed` e regravava esse perfil em `.ue4cache/u/dbdm_store/accounts.json`.
- Além disso, em desligamentos normais da VM (`ShutdownThread`), o `SettingsProvider` descarrega o `SettingsState` da RAM para o disco, podendo sobrescrever a edição feita em `/data/system/users/0/settings_ssaid.xml`.

### 2.2. Estrutura XML Texto Puro e `userkey` no Android 11
- No Android 11, quando o pacote `com.netease.dbdena` ainda não possui uma tag `<setting ... package="com.netease.dbdena" ... />` materializada no `settings_ssaid.xml`, o `SettingsProvider` deriva o SSAID deterministicamente via `HMAC-SHA256(userkey, assinatura_do_app)`.
- Sem rotacionar a chave `userkey` (64 hex) e sem injetar explicitamente a tag `<setting>` com o UID numérico real do pacote quando ausente, o `SettingsProvider` regenerava sempre o mesmo SSAID.
- Adicionalmente, a janela de busca em `findSsaidForPackage` (`-0x34` / `+0x50` bytes) podia falhar caso houvesse atributos adicionais antes de `package=` ou alcançar a linha `<setting>` do aplicativo seguinte no XML texto puro.

### 2.3. Isolamento de Pontos de Montagem (Scoped Storage / FUSE) no Android 11
- No Android 11, `/sdcard/Android/data` passa pelo daemon FUSE do `MediaProvider`, onde operações `cp` podem falhar por preservação de atributos e onde os caminhos reais subjacentes incluem `/mnt/runtime/write/emulated/0`, `/mnt/pass_through/0/emulated/0`, `/mnt/user/0/emulated/0` e `/data/media/0`.

---

## 3. Correções Implementadas (`SsaidManager.smali`)

1. **Despejo e Recarregamento Instantâneo do Cache RAM do `SettingsProvider` (Zero Reboot):**
   - Após gravar o novo `/data/system/users/0/settings_ssaid.xml`, o arquivo é protegido temporariamente com `chattr +i` e é enviado o broadcast direcionado exclusivamente ao `SettingsProvider`:
     ```sh
     am broadcast -a android.intent.action.USER_STOPPED --ei android.intent.extra.user_handle 0 -p com.android.providers.settings
     ```
   - No AOSP (`SettingsProvider.java`), esse broadcast executa `mSettingsRegistry.removeUserStateLocked(0, false)`, removendo imediatamente `ssaidKey` do mapa `mSettingsStates` na RAM do `system_server` sem afetar outros serviços (`-p com.android.providers.settings`).
   - Em seguida, uma consulta `content query --uri content://settings/secure --projection value --where "name='android_id'"` é executada sob o UID do jogo (`su $auid -c ...`), forçando `ensureSettingsStateLocked(ssaidKey)` a **recarregar imediatamente o novo `settings_ssaid.xml` do disco para a RAM do `system_server`**.

2. **Injeção e Sanitização Completa de `settings_ssaid.xml` (Android 11 XML + Android 12–16 ABX):**
   - Em arquivos XML texto puro (`<settings>`):
     - Gera nova `userkey` de 64 caracteres hexadecimais e atualiza `name="userkey"`.
     - Atualiza `value="<novo_ssaid>"`, `defaultValue="<novo_ssaid>"` e `name="<uid_do_jogo>"` na entrada de `com.netease.dbdena`.
     - Insere automaticamente `<setting id="99" name="$auid" value="$n" package="com.netease.dbdena" defaultValue="$n" defaultSysSet="false" tag="null" />` antes de `</settings>` caso a entrada não exista, ou recria a estrutura XML completa caso o arquivo esteja vazio.
   - Ajustada a janela de `findSsaidForPackage` para `-0x60` bytes (retroativo) e `+0x28` bytes (progressivo), evitando colisão com tags `<setting>` vizinhas.

3. **Cobertura Completa de Mounts Android 11 / Emuladores para `accounts.json` e `.ue4cache`:**
   - Adicionados `/mnt/runtime/write/emulated/0`, `/mnt/runtime/default/emulated/0`, `/mnt/pass_through/0/emulated/0`, `/mnt/user/0/emulated/0`, `/mnt/sdcard`, `/data/media/0`, `/data/media` e `/proc/1/root/data/media/0` na limpeza, leitura e escrita de `accounts.json`, utilizando redirecionamento de stream (`cat "$t" > ...`) e grupo `1078` (`ext_data_rw`).
