# Guia Completo de Uso e Instalação do Módulo (`ue4_instrument_zygisk.zip`)

Este guia explica passo a passo como usar os binários já compilados na pasta [`modulos/`](file:///c:/Users/walker/Desktop/ida95/modulos), como instalar o módulo no Android via **Magisk / Zygisk** (pelo PC com ADB ou direto pelo celular), e como monitorar os logs no `logcat`.

---

## 1. Arquivos Compilados Prontos na Pasta `modulos/`

- **[`ue4_instrument_zygisk.zip`](file:///c:/Users/walker/Desktop/ida95/modulos/ue4_instrument_zygisk.zip)** (`152.9 KB`) — Módulo Magisk/Zygisk pronto para instalar no Android (`arm64-v8a`).
- **[`libinstrument_core.so`](file:///c:/Users/walker/Desktop/ida95/modulos/libinstrument_core.so)** (`224.9 KB`) — Biblioteca nativa compilada a partir de [`entry.cpp`](file:///c:/Users/walker/Desktop/ida95/entry.cpp) com NDK `r27c` (`Clang 18.0.3`, `-std=c++20 -O3 -flto=thin`), exportando exclusivamente:
  - `JNI_OnLoad` (`0x136b8`)
  - `JNI_OnUnload` (`0x1371c`)
  - `zygisk_module_entry` (`0x135d8`)
- **[`libue4_diagnostics.so`](file:///c:/Users/walker/Desktop/ida95/modulos/libue4_diagnostics.so)** (`135.1 KB`) — Biblioteca da suíte modular completa ([`lib_main.cpp`](file:///c:/Users/walker/Desktop/ida95/lib_main.cpp)) com `.init_array` e API C (`UE4Diag_*`).
- **`ue4_instrument_zygisk/`** — Pasta com o conteúdo descompactado do módulo.

---

## 2. Pré-Requisitos no Android / Emulador

1. **Arquitetura:** 64 bits ARM (`arm64-v8a` / ELF64 AArch64).
2. **Magisk v24.0+ com Zygisk ativado** (ou KernelSU + módulo ZygiskNext):
   - Abra o app **Magisk** → toque no ícone de **Configurações** (canto superior direito).
   - Ative **Zygisk**.
   - **Atenção:** Se a **Lista de Negação (DenyList)** estiver ativada, certifique-se de que o pacote **`com.netease.dbdena` NÃO está marcado** na lista de negação (caso contrário, o Zygisk bloqueia o carregamento de módulos no jogo).

---

## 3. Como Instalar o Módulo (`ue4_instrument_zygisk.zip`)

### Opção 1 — Pelo PC via ADB (Mais Rápido)
Abra o PowerShell na raiz do projeto (`c:\Users\walker\Desktop\ida95`) e rode:

```powershell
# 1. Envia o zip para o dispositivo
adb push .\modulos\ue4_instrument_zygisk.zip /data/local/tmp/ue4_instrument_zygisk.zip

# 2. Instala via Magisk CLI (requer root su)
adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"

# 3. Reinicia o aparelho para ativar o módulo no Zygote64
adb reboot
```

### Opção 2 — Direto no Celular / Emulador (Sem PC)
1. Envie o arquivo [`modulos/ue4_instrument_zygisk.zip`](file:///c:/Users/walker/Desktop/ida95/modulos/ue4_instrument_zygisk.zip) para a pasta `Download` do celular.
2. Abra o aplicativo **Magisk** → vá na aba **Módulos** (canto inferior direito).
3. Toque em **Instalar a partir do armazenamento** e escolha `ue4_instrument_zygisk.zip`.
4. Toque em **Reiniciar**.

---

## 4. Como Verificar o Funcionamento em Tempo Real (`adb logcat`)

Com o celular reiniciado e conectado ao PC via ADB:

### 1. Limpe o buffer de log e abra o jogo (`com.netease.dbdena`)
```powershell
adb logcat -c
adb shell "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1"
```

### 2. Monitore os logs da `.so` em tempo real
```powershell
adb logcat -v threadtime -s UE4Diag:V Zygisk:V DEBUG:V libc:W
```

Assim que o jogo iniciar, você verá no terminal:
```text
I UE4Diag : Zygisk postAppSpecialize matched com.netease.dbdena; starting worker.
I UE4Diag : Initializing UE4.27 AArch64 Instrumentation (Configured RVAs: GWorld=0x10D69998, GNamePool=0x10B7C100)
I UE4Diag : Telemetry worker started; polling dl_iterate_phdr for libUE4.so...
I UE4Diag : libUE4.so resolved | Base=0x78B4000000 | MappedSize=0x10DF0000 (269 MB)
I UE4Diag : Anchors | GWorld=0x78C4D69998 (RVA +0x10D69998 -> *0x78124A0000) | GNamePool=0x78C4B7C100 (RVA +0x10B7C100, Init=1, CurBlock=6)
I UE4Diag : Globals | GUObjectArray=0x78C4BF7920 (+0x10BF7920) | GEngine=0x78C4D65798 (+0x10D65798 -> *0x780F8C1200)
I UE4Diag : GNamePool verified @ 0x78C4B7C100 | EName(0) resolved to "None"
I UE4Diag : World state active | UWorld=0x78124A0000 | PersistentLevel=0x7814902100 (Actors=642) | OwningGameInstance=0x780E310800
```

### 3. Comandos extras para checar memória e thread ativa
```powershell
# Verifica se libUE4.so e arm64-v8a.so estão carregadas no processo
adb shell "su -c 'PID=`$(pidof com.netease.dbdena); cat /proc/`$PID/maps | grep -E \"libUE4|libinstrument_core|arm64-v8a\"'"

# Verifica se a thread ue4_diag_worker está rodando
adb shell "su -c 'PID=`$(pidof com.netease.dbdena); ps -T -p `$PID | grep ue4_diag_worker'"
```

---

## 5. Como Recompilar se Você Editar o Código

O Android NDK `r27c` já está instalado em `C:\Android\ndk\android-ndk-r27c` e configurado no [`build_module.ps1`](file:///c:/Users/walker/Desktop/ida95/build_module.ps1).
Sempre que alterar qualquer valor em [`entry.cpp`](file:///c:/Users/walker/Desktop/ida95/entry.cpp), basta abrir o PowerShell na pasta `c:\Users\walker\Desktop\ida95` e rodar:

```powershell
.\build_module.ps1
```

Ele recompila tudo em poucos segundos e atualiza automaticamente os arquivos dentro da pasta [`modulos/`](file:///c:/Users/walker/Desktop/ida95/modulos).

