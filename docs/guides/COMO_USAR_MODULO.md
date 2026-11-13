# Guia Completo de Uso e Instalação do Módulo (`ue4_instrument_zygisk.zip`)

Este guia explica passo a passo como usar o módulo compilado na pasta [`modulos/`](file:///c:/Users/walker/Desktop/ida95/modulos), como instalá-lo no Android via **Magisk / Zygisk** (ou via `adb`), e como verificar os logs em tempo real no `logcat`.

---

## 1. Onde estão os arquivos compilados?

Dentro da pasta [`modulos/`](file:///c:/Users/walker/Desktop/ida95/modulos) na raiz do projeto estão:

```text
modulos/
├── ue4_instrument_zygisk.zip              # Pacote flashável pronto para instalar no Magisk (Zygisk)
├── libinstrument_core.so                  # Biblioteca ELF64 AArch64 (JNI_OnLoad + zygisk_module_entry)
├── libue4_diagnostics.so                  # Biblioteca da suíte completa (Steps 1 a 6 + API C UE4Diag_*)
└── ue4_instrument_zygisk/                 # Estrutura descompactada do módulo Magisk/Zygisk
    ├── META-INF/com/google/android/
    │   ├── update-binary
    │   └── updater-script
    ├── module.prop
    ├── customize.sh
    ├── system/lib64/
    │   └── libinstrument_core.so
    └── zygisk/
        └── arm64-v8a.so
```

---

## 2. Pré-Requisitos no Dispositivo Android / Emulador

1. **Arquitetura:** ARM64 (`arm64-v8a` / ELF64 AArch64).
2. **Root com Magisk v24.0+ (ou KernelSU + ZygiskNext):**
   - Abra o aplicativo **Magisk** → toque na engrenagem (**Configurações**).
   - Ative a opção **Zygisk** (`Habilitar Zygisk`).
   - *(Importante)* Se a **Lista de Negação (DenyList)** estiver ativa, **desmarque** `com.netease.dbdena` da lista de negação (ou desative "Seguir Lista de Negação"), pois o Zygisk não injeta módulos em processos que estão na DenyList.
3. **Jogo instalado:** Dead by Daylight Mobile (`com.netease.dbdena` — Build `292949`, `libUE4.so`).

---

## 3. Como Instalar o Módulo (`ue4_instrument_zygisk.zip`)

### Opção A — Instalação Automática via ADB (No PC)

Abra o PowerShell na pasta `c:\Users\walker\Desktop\ida95` e execute:

```powershell
# 1. Envia o módulo pronto da pasta modulos/ para o celular/emulador
adb push .\modulos\ue4_instrument_zygisk.zip /data/local/tmp/ue4_instrument_zygisk.zip

# 2. Instala o módulo via Magisk CLI (requer permissão root no shell)
adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"

# 3. Reinicia o aparelho para carregar o módulo no Zygote64
adb reboot
```

### Opção B — Instalação Manual pelo App do Magisk (No Celular)

1. Copie o arquivo [`modulos/ue4_instrument_zygisk.zip`](file:///c:/Users/walker/Desktop/ida95/modulos/ue4_instrument_zygisk.zip) para a memória interna do celular (`/sdcard/Download/`).
2. Abra o app **Magisk** → aba **Módulos** (ícone de quebra-cabeça no canto inferior direito).
3. Toque em **Instalar a partir do armazenamento** e selecione `ue4_instrument_zygisk.zip`.
4. Toque em **Reiniciar** ao concluir.

---

## 4. Como Testar e Ver os Logs em Tempo Real (`adb logcat`)

Assim que o celular reiniciar:

### Passo 1 — Limpar o log e abrir o jogo
```powershell
adb logcat -c
adb shell "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1"
```

### Passo 2 — Acompanhar a telemetria e validação dos offsets no terminal
```powershell
adb logcat -v threadtime -s UE4Diag:V Zygisk:V DEBUG:V libc:W
```

Você verá no terminal exatamente a sequência de confirmação:
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

### Passo 3 — Confirmar se a `.so` e a thread `ue4_diag_worker` estão ativas no processo
```powershell
# Verifica o mapeamento em /proc/<pid>/maps
adb shell "su -c 'PID=`$(pidof com.netease.dbdena); cat /proc/`$PID/maps | grep -E \"libUE4|libinstrument_core|arm64-v8a\"'"

# Verifica a thread em segundo plano rodando a ~60 Hz
adb shell "su -c 'PID=`$(pidof com.netease.dbdena); ps -T -p `$PID | grep ue4_diag_worker'"
```

---

## 5. Como Recompilar Quando Alterar Algum Offset ou Parâmetro

1. Edite os valores desejados em [`entry.cpp`](file:///c:/Users/walker/Desktop/ida95/entry.cpp) (ex.: velocidade em `apply_kinematic_limits`, nome em `override_local_name`, ou offsets em `EngineOffsets`).
2. No PowerShell, execute:
   ```powershell
   .\build_module.ps1
   ```
   O script compila a nova `.so`, faz o *strip* de símbolos internos, verifica as exportações com `llvm-nm` e atualiza automaticamente o pacote em [`modulos/ue4_instrument_zygisk.zip`](file:///c:/Users/walker/Desktop/ida95/modulos/ue4_instrument_zygisk.zip).

---

## 6. Como Desativar ou Remover o Módulo

Caso queira desativar ou remover o módulo via ADB sem precisar abrir o app do Magisk:

```powershell
# Desativar temporariamente (cria o arquivo 'disable')
adb shell "su -c 'touch /data/adb/modules/ue4_instrument_core/disable'"

# Remover completamente no próximo boot
adb shell "su -c 'touch /data/adb/modules/ue4_instrument_core/remove && reboot'"
```

