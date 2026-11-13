# Análise Minuciosa e Completa: Dead by Daylight Mobile (APK)

**Data da Análise:** 09/11/2026  
**Chave AES de Criptografia UE4 PAK Fornecida:** `0x299BB5D348F021E966134382D676758F02726DF38F47DD96BE191AFC464DBC82`

---

## 1. Identificação Geral do Pacote

| Propriedade | Valor |
| :--- | :--- |
| **Nome da Aplicação** | Dead by Daylight Mobile (`DeadByDaylight`) |
| **Package ID** | `com.netease.dbdena` |
| **Versão / VersionCode** | `1.292949.292949` / `292949` |
| **Target SDK / Min SDK** | `33` (Android 13) / `24` (Android 7.0+) |
| **Motor Gráfico (Engine)** | Unreal Engine 4.27.0 (`++UE4+Release-4.27-DBD`) |
| **Configuração de Build** | `Shipping` (Otimizado para produção) |
| **Formato de Texturas** | `ASTC` |
| **Arquitetura Nativa Suportada** | `arm64-v8a` |
| **Instalação** | `internalOnly` (Não permite cartão SD por padrão) |
| **Distribuição** | Split APKs / Google Play App Bundle (`base.apk`, `split_obbassets.apk`, `split_pakchunks.apk`, splits de linguagem e densidade) |

---

## 2. Estrutura de Arquivos e Formato do Jogo

O Dead by Daylight Mobile utiliza um formato em camadas para entrega de conteúdo no Android:

1. **`base.apk` (14.55 MB):** Contém o código Java/Smali, manifest, bridges de JavaScript, recursos básicos de UI e arquivos de configuração dos SDKs.
2. **`split_config.arm64_v8a.apk` (282.98 MB):** Contém os binários nativos de 64 bits:
   - `libUE4.so` (251.44 MB) - O motor Unreal Engine 4 compilado com as lógicas completas de gameplay, física, renderização e networking.
   - `libdetect.so` (3.08 MB) & `libdbdmguard.so` / `libngdetect-jni-lib.so` - Bibliotecas proprietárias da NetEase de anti-cheat, integridade e detecção de root/emulador.
   - `libAudioCommon.so`, `libAudioCCReName.so`, `libAudioEngine.so`, `libAudioMNN.so`, plugins Wwise (`libAk*.so`).
   - `libunitrace_memory_advice.so` (4.46 MB) - Otimização de gerenciamento de memória em tempo real.
3. **`split_obbassets.apk` (3.43 GB) & `split_pakchunks.apk` (1.48 GB):**
   - Os arquivos de dados reais do jogo estão encapsulados sob a extensão disfarçada `.obb.png` (`assets/main.obb.png`, `assets/patch.obb.png`, `assets/chunks.obb.png`).
   - Cada um desses arquivos `.obb.png` é, na realidade, um arquivo ZIP padrão contendo a árvore de arquivos `.pak` e `.index` da Unreal Engine:
     - `DeadByDaylight/Content/Paks/pakchunk0-Android_ASTC.pak` (764 MB)
     - `DeadByDaylight/Content/Paks/pakchunk1-Android_ASTC.pak` (307 MB)
     - `DeadByDaylight/Content/Lite/pakchunk1001-Android_ASTC.pak` (666 MB)
     - `DeadByDaylight/Content/Lite/pakchunk1001optional-Android_ASTC.pak` (1.08 GB)
     - Arquivos de Script, Shaders, Áudio e Texturas.
   - **Algoritmo de Compressão:** Oodle (verificado pelo header `Oodle` nos índices de PAK).
   - **Criptografia PAK:** Criptografados via AES-256 usando a chave `0x299BB5D348F021E966134382D676758F02726DF38F47DD96BE191AFC464DBC82`.

---

## 3. Configurações de Gameplay e Unreal Engine 4

Extraído diretamente do `AndroidManifest.xml` e do `UE4CommandLine.txt`:

- **Linha de Comando Inicial:** `../../../DeadByDaylight/DeadByDaylight.uproject`
- **Profundidade de Z-Buffer:** 24-bit (`DepthBufferPreference = 24`)
- **Suporte Vulkan:** Desativado (`bSupportsVulkan = false`, opera estritamente via OpenGL ES 3.1)
- **Cutout / Notch:** Suportado e adaptável tanto para retrato quanto paisagem (`portrait|landscape`)
- **Validação de OBB:** `bVerifyOBBOnStartUp = true`
- **Validação de Texturas:** `bValidateTextureFormats = true`
- **Arquivos de Log Públicos:** `bPublicLogFiles = true` (Permite geração de logs para depuração de crashes)
- **Modos de Desempenho do Sistema Android:** Habilitados (`gamemode.performance.enabled = true`, `gamemode.battery.enabled = true`)

---

## 4. Endpoints de Rede, Servidores e Domínios

Os seguintes servidores e endpoints foram identificados durante o rastreamento estático:

### A. Autenticação e Gestão de Conta (NetEase Mpay Overseas)
- **Servidor Primário:** `https://sdk-os.mpsdk.easebar.com`
- **Servidor Secundário / Oriente Médio:** `https://sdk-os-me.mpsdk.easebar.com`
- **Ambiente de Testes / Sandbox:** `https://mgbsdksgtest.matrix.easebar.com/ma100ena/sdk/` e `https://mgbsdksgtest.matrix.easebar.com/ma100naxx2jp/sdk/`

### B. Telemetria, Crash Report e Diagnósticos
- **Envio de Crash Dumps:** `https://appdump.nie.easebar.com/upload`
- **Registro de Dispositivos / Device Info:** `https://appdump.nie.easebar.com/device` (CDN: `https://unisdk.update.easebar.com/ngdevice/`)
- **MCount Tracker (Estatísticas de Gameplay):** `https://mcount.easebar.com`
- **AppLog Matrix:** `https://applog.matrix.easebar.com`
- **Feature Flag Switcher:** `https://g0.gsf.easebar.com/feature/query.json`

### C. Serviços Geográficos e Anticheat
- **Localização / LBS:** `https://nglbs.nie.netease.com:8000/`

---

## 5. Chaves de API, Tokens e Credenciais de SDKs Extraídas

Todas as chaves de integração e credenciais dos SDKs embutidos foram localizadas nos manifestos, recursos XML e assets Base64 decodificados:

### A. Google e Firebase
- **Google API Key:** `AIzaSyC0RJx1XbuyDPg84OXIUwafF51P4x7ihRo`
- **Google App ID:** `1:296478348732:android:cf66adefe4d068a804c7b9`
- **Google Web Client ID:** `296478348732-ouutfnm5fv1u02cn0bbeokn6hmsi148c.apps.googleusercontent.com`
- **Google Play Games App ID:** `463602023920`

### B. Facebook / Meta
- **Facebook Client Token:** `be894c931344fb1595e4b44be17c7a01`
- **AutoLog & ID Collection:** Ativado (`AutoLogAppEventsEnabled = true`, `AdvertiserIDCollectionEnabled = true`)

### C. Twitter / X SDK
- **Twitter Consumer Key:** `LajmI5TFh9Jntn8VGiqPKHMIB`
- **Twitter Consumer Secret:** `sWZr6aKc3W8jovngUNfCpw9r6YB8SpLtY60c2JLulLvy4wTSzq`

### D. AppsFlyer & UniSDK NetEase
- **AppsFlyer Dev Key (Módulo Principal):** `qCGvc4O6re5PfOFzimdgle`
- **AppsFlyer Dev Key (Analytics Alternativo):** `tEJmf9X4rd8YcXAgykazud`
- **AppsFlyer Dev Key (Plugin):** `tDGhd4W5ia7VcWBvukfzxa`
- **AppsFlyer Dev Key (AdvertBase):** `mAKia8N7ke4ObNFgznfhpe`
- **AppsFlyer Dev Key (PlayCore):** `lBGjc3U5xe2YfUAyvhdize`
- **AppsFlyer Dev Key (Protocol):** `uCXyb6Z5re8SfZEktndhoe`
- **Client Key NetEase Protocol:** `JPn9v3npSOSvNqtXfh/3aQ==`
- **Product ID:** `ma100ena_2` (Versão Global/América)

---

## 6. Módulos de Segurança, Anti-Cheat e Integridade

A estrutura de proteção do APK é composta por três camadas principais:

1. **Camada Nativa (`libdbdmguard.so` e `libdetect.so`):**
   - Responsável por inspecionar a memória do processo, detectar hooks de Frida/Xposed e identificar depuradores gdb/lldb anexados.
   - Valida assinaturas de código e integridade da `libUE4.so`.
2. **Camada Java (`SdkNgDetect` / `ngdetect_data`):**
   - Rastreia ambiente de execução: root (SuperSU, Magisk, KernelSU), pacotes de clonagem de apps, e emuladores conhecidos (BlueStacks, LDPlayer, Nox).
3. **Mecanismo OBB / PAK Encryption:**
   - Para evitar alteração de modelos, texturas (ex: remover névoa, alterar modelos de assassinos ou sobreviventes) ou tabelas de dados de atributos, os arquivos `.pak` utilizam criptografia de bloco AES-256 e compactação Oodle com validação de hash em tempo de carregamento.

---

## 7. Instruções para Descompactação e Extração dos PAKs

Para extrair e modificar os dados internos (uassets, tabelas de dados de personagens, sons, etc.) utilizando a chave AES:

1. Extraia o arquivo `assets/main.obb.png` ou `assets/patch.obb.png` de `dbdmonlin/split_obbassets.apk` e renomeie para `.zip`.
2. Descompacte os arquivos `.pak` e `.index`.
3. Utilize ferramentas como `FModel` ou `quickbms` com o script de Unreal Engine 4:
   - **Versão da Unreal:** Selecionar `4.27`.
   - **Chave de Criptografia AES:** Informar exatamente a chave:  
     `0x299BB5D348F021E966134382D676758F02726DF38F47DD96BE191AFC464DBC82`
   - O FModel carregará a árvore de diretórios completa de `DeadByDaylight/Content/...` permitindo exportar modelos 3D, áudios Wwise, tabelas de dados JSON e configurações de gameplay.

