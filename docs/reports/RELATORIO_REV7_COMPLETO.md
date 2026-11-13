# RELATÓRIO TÉCNICO CONSOLIDADO — REV. 7 (`libUE4.so` / ARM64)
**Alvo:** Dead by Daylight Mobile (NetEase — `com.netease.dbdena`, v1.292949 / Build `292949`)  
**Engine:** Unreal Engine 4.27.0 (`++UE4+Release-4.27-DBD`, ARM64 ELF64, `263.651.496` bytes / ~251,44 MB)  
**Data da Sessão:** 10/11/2026  
**Escopo:** Execução integral das filas **T1–T8** e **T9–T16** sob contrato de evidência estrita (Grau A/B/C/D, endereço absoluto, bytes crus, instrução ARM64 e efeito direto).

---

## SUMÁRIO EXECUTIVO E VERDADE DE BASE CONSOLIDADA (GRAU A)

| Símbolo / Estrutura | Endereço Estático / Offset | Slot GOT | Prova Primária no Binário | Grau |
|---|---|---|---|---|
| `vtable_UObjectBase` | `0x0EF88638` (`.data.rel.ro`) | `0x0F6685C0` (`-0x10`) | `0x0A76398C` (`UObjectBase::UObjectBase`) | **A** |
| `vtable_UObject` | `0x0EF8A668` (`.data.rel.ro`) | `0x0F66ED50` (`-0x10`) | `0x0A6F4338`–`0x0A6F4344`, `0x0A6F4388`–`0x0A6F4390` | **A** |
| `GUObjectArray` (`FUObjectArray`) | **`0x10BF7920`** (`.bss`) | **`0x0F6685C8`** | `0x0A7639F8`–`0x0A7639FC` (`UObjectBase::AddObject`) | **A** |
| `GUObjectArray.ObjObjects` (`FChunkedFixedUObjectArray`) | **`0x10BF7930`** (`.bss`) | — (`+0x10`) | `0x0A7632BC`, `0x0A763420`, `0x0A763A48` | **A** |
| `sizeof(FUObjectItem)` | **`0x18` (24 B)** | — | `0x0A7632D8` (`ADD X8, X8, #0x18`), `0x0A763A68` (`MOV W10, #0x18; SMADDL`) | **A** |
| `UObjectBase::OuterPrivate` | **`+0x28`** (`sizeof(UObjectBase) = 0x30`) | — | `0x0A763998` (`STR XZR, [X0, #0x28]`) | **A** |
| `GNamePool` (`FNamePool`) | **`0x10B7C100`** (`.bss`) | — | `0x0A59F82C`–`0x0A59F868` (`FName::ToString`), `0x0A59ABDC` (`FNamePool::FNamePool`) | **A** |
| `bNamePoolInitialized` | **`0x10BCF0C0`** (`.bss`) | — | `0x0A59F818`–`0x0A59F81C` (`LDRB W8, [X8, #0xC0]`) | **A** |
| `GWorld` (`UWorldProxy`) | **`0x10D69998`** (`.bss`) | **`0x0F668440`** | `0x04BC9678`–`0x04BC9680`, `0x0C7108E0`–`0x0C7108F0` (`19` refs em `.text`) | **A** |
| `GEngine` (`UEngine*`) | **`0x10D65798`** (`.bss`) | **`0x0F6682A8`** | `0x0C1BFDDC`–`0x0C1BFDEC` (`UGameplayStatics::GetGameInstance`) | **A** |
| `UWorld::StaticClass()` singleton | **`0x10D953E8`** (`.bss`) | — | `0x0CA4C76C`–`0x0CA4C780` (`UWorld::StaticClass`) | **A** |
| `GIsRetrievingVTablePtr` | **`0x10B66F30`** (`.bss`) | **`0x0F66ED58`** | `0x0A6F4348`–`0x0A6F4350`, `0x0A6F4370`–`0x0A6F4378` (`2` refs em `.text`) | **A** |
| `LogUObjectGlobals` | **`0x10B66CF4`** (`.bss`) | **`0x0F668668`** | `0x0A6F43B0`–`0x0A6F43B4` (`+0x04 = CategoryName` em `0x10B66CF8`, `38` refs) | **A** |
| `GError` (`FOutputDeviceError*`) | **`0x10B65FA0`** (`.bss`) | **`0x0F6694D0`** | `0x0A50C518`–`0x0A50C52C` (`10` refs em `.text`) | **A** |
| `GErrorHist` (`wchar_t[16384]`) | **`0x10B74118`** (`.bss`) | **`0x0F669598`** | `0x0A50C520`–`0x0A50C530` (`53` refs em `.text`) | **A** |
| `GIsCriticalError` | **`0x10B39D88`** (`.bss`) | — | `0x0A50C528` + `0x0A50C53C` (`STRB W9, [X8, #0xD88]`) | **A** |

---

# PARTE I — FILA T1–T8 (ARQUITETURA `UOBJECT`, CONSTRUTORES, THREAD CONTEXT E `GUOBJECTARRAY`)

## T1 — String de formato (`0x02AF6022` e correção do alvo real em `0x0A6F43BC` + `0x0A6F43C4`)
- **Grau:** **A**.

### 1.1 Leitura literal de `0x02AF6022` (`UTF-16LE`, `408` B até `\0\0`)
- **`0x02AF6022`–`0x02AF6121` (256 B):**
  `55004f0062006a00650063007400280063006f006e0073007400200046004f0062006a0065006300740049006e0069007400690061006c0069007a006500720026002900200063006f006e007300740072007500630074006f0072002000630061006c006c006500640020006200750074002000690074002700730020006e006f007400200074006800650020006f0062006a0065006300740020007400680061007400270073002000630075007200720065006e0074006c00790020006200650069006e006700200063006f006e00730074007200750063007400650064002000770069007400680020004e00650077004f0062006a006500630074002e00`
- **`0x02AF6122`–`0x02AF61BA` (152 B):**
  `20004d006100790062006500200079006f0075002000610072006500200074007200790069006e006700200074006f00200063006f006e0073007400720075006300740020006900740020006f006e002000740068006500200073007400610063006b002c0020007700680069006300680020006900730020006e006f007400200073007500700070006f0072007400650064002e000000`
- **Texto literal decodificado (`0x02AF6022`):**
  > `"UObject(const FObjectInitializer&) constructor called but it's not the object that's currently being constructed with NewObject. Maybe you are trying to construct it on the stack, which is not supported."`

### 1.2 Correção do par `ADRP`/`ADD` em `0x0A6F43BC` + `0x0A6F43C4`
No bloco `0x0A6F43A8`, a carga de `X4` (`Fmt`) resolve para **`0x02AD368E`**:
- `0x0A6F43BC`: `e4 1e fc f0` (`0xF0FC1EE4`) → `immlo = 3`, `immhi = 0x7E0F7`, `imm21 = -0x7C21` páginas → `0x0A6F4000 - 0x07C21000 = 0x02AD3000` (`ADRP X4, #0x02AD3000`).
- `0x0A6F43C4`: `84 38 1a 91` (`0x911A3884`) → `imm12 = 0x68E` (`ADD X4, X4, #0x68E`) → **`X4 = 0x02AD368E`**.
- **`0x02AD368E`–`0x02AD3739` (`172` B, `UTF-16LE`):**
  `5700650020006100720065002000630075007200720065006e0074006c0079002000720065007400720069006500760069006e006700200056005400610062006c00650020007000740072002e00200050006c00650061007300650020007500730065002000460056005400610062006c006500480065006c00700065007200200063006f006e007300740072007500630074006f007200200069006e00730074006500610064002e000000`
- **Texto literal decodificado (`0x02AD368E`):**
  > `"We are currently retrieving VTable ptr. Please use FVTableHelper constructor instead."`
- **Veredito T1:** `sub_A6F4370` é `UObject::UObject()` (`UObjectGlobals.cpp:113` = `0x71`), que executa `UE_CLOG(GIsRetrievingVTablePtr, LogUObjectGlobals, Fatal, TEXT("We are currently retrieving VTable ptr. Please use FVTableHelper constructor instead."))`. O global `0x10B66F30` (via GOT `0x0F66ED58`) é `bool GIsRetrievingVTablePtr`.

---

## T2 — Categoria de log e globais de `0x0A6F4370` / `0x0A6F43A8`
- **Grau:** **A**.

| Endereço | Bytes | Instrução | Alvo / Resolução | Efeito | Grau |
|---|---|---|---|---|---|
| `0x0A6F4370` | `c8 7b 02 d0` | `ADRP X8, #0x0F66E000` | Página GOT `0x0F66E000` | Base GOT para `GIsRetrievingVTablePtr` | A |
| `0x0A6F4374` | `08 ad 46 f9` | `LDR X8, [X8, #0xD58]` | `[0x0F66ED58]` = `306fb61000000000` → `0x10B66F30` | `X8 = &GIsRetrievingVTablePtr` (`.bss`) | A |
| `0x0A6F4378` | `08 01 40 39` | `LDRB W8, [X8]` | `*(uint8_t*)0x10B66F30` | Lê `GIsRetrievingVTablePtr` antes do `CBZ` | A |
| `0x0A6F43B0` | `a8 7b 02 90` | `ADRP X8, #0x0F668000` | Página GOT `0x0F668000` | Base GOT para categoria de log | A |
| `0x0A6F43B4` | `08 35 43 f9` | `LDR X8, [X8, #0x668]` | `[0x0F668668]` = `f46cb61000000000` → `0x10B66CF4` | `X8 = &LogUObjectGlobals` (`.bss`) | A |
| `0x0A6F43C8` | `02 11 00 91` | `ADD X2, X8, #0x04` | `0x10B66CF4 + 4 = 0x10B66CF8` | `X2 = &LogUObjectGlobals.CategoryName` (`const FName&`) | A |

---

## T3 — Listing completo `0x0A6F4314`–`0x0A6F43E0`
- **Grau:** **A**.

| Endereço | Bytes | Instrução | Efeito |
|---|---|---|---|
| `0x0A6F4314` | `f3 0f 1e f8` | `STR X19, [SP, #-0x20]!` | Prólogo de `UObject::UObject(EStaticConstructor, EObjectFlags)` |
| `0x0A6F4318` | `fd 7b 01 a9` | `STP X29, X30, [SP, #0x10]` | Salva FP/LR |
| `0x0A6F431C` | `fd 43 00 91` | `ADD X29, SP, #0x10` | Atualiza FP |
| `0x0A6F4320` | `5f 00 06 72` | `TST W2, #0x1` | Testa flag |
| `0x0A6F4324` | `88 10 80 52` | `MOV W8, #0x84` | `RF_NeedLoad \| RF_NeedPostLoad` |
| `0x0A6F4328` | `08 01 9f 1a` | `CSEL W8, W8, WZR, EQ` | Seleciona máscara |
| `0x0A6F432C` | `01 01 02 2a` | `ORR W1, W8, W2` | Combina `InFlags` |
| `0x0A6F4330` | `f3 03 00 aa` | `MOV X19, X0` | `X19 = this` |
| `0x0A6F4334` | `96 bd 01 94` | `BL 0x0A76398C` | Chama `UObjectBase::UObjectBase(EObjectFlags)` |
| `0x0A6F4338` | `c8 7b 02 d0` | `ADRP X8, #0x0F66E000` | Página GOT `0x0F66E000` |
| `0x0A6F433C` | `08 a9 46 f9` | `LDR X8, [X8, #0xD50]` | `[0x0F66ED50]` = `0x0EF8A658` |
| `0x0A6F4340` | `08 41 00 91` | `ADD X8, X8, #0x10` | `X8 = 0x0EF8A668` (`vtable_UObject`) |
| `0x0A6F4344` | `68 02 00 f9` | `STR X8, [X19]` | `this->vtable = 0x0EF8A668` |
| `0x0A6F4348` | `c8 7b 02 d0` | `ADRP X8, #0x0F66E000` | Página GOT `0x0F66E000` |
| `0x0A6F434C` | `08 ad 46 f9` | `LDR X8, [X8, #0xD58]` | `[0x0F66ED58]` = `0x10B66F30` (`&GIsRetrievingVTablePtr`) |
| `0x0A6F4350` | `08 01 40 39` | `LDRB W8, [X8]` | `W8 = GIsRetrievingVTablePtr` |
| `0x0A6F4354` | `88 00 00 34` | `CBZ W8, 0x0A6F4364` | Se 0, retorna normalmente |
| `0x0A6F4358` | `fd 7b 41 a9` | `LDP X29, X30, [SP, #0x10]` | Restaura FP/LR |
| `0x0A6F435C` | `f3 07 42 f8` | `LDR X19, [SP], #0x20` | Restaura `X19` |
| `0x0A6F4360` | `12 00 00 14` | `B 0x0A6F43A8` | Salta para o bloco Fatal compartilhado `0x0A6F43A8` |
| `0x0A6F4364` | `fd 7b 41 a9` | `LDP X29, X30, [SP, #0x10]` | Epílogo normal |
| `0x0A6F4368` | `f3 07 42 f8` | `LDR X19, [SP], #0x20` | Epílogo normal |
| `0x0A6F436C` | `c0 03 5f d6` | `RET` | Fim de `0x0A6F4314` |
| **`0x0A6F4370`** | `c8 7b 02 d0` | `ADRP X8, #0x0F66E000` | Início de `UObject::UObject()` (`sub_A6F4370`) |
| **`0x0A6F4374`** | `08 ad 46 f9` | `LDR X8, [X8, #0xD58]` | `X8 = [0x0F66ED58] = 0x10B66F30` (`&GIsRetrievingVTablePtr`) |
| **`0x0A6F4378`** | `08 01 40 39` | `LDRB W8, [X8]` | `W8 = GIsRetrievingVTablePtr` |
| **`0x0A6F437C`** | `48 00 00 34` | `CBZ W8, 0x0A6F4384` | Se 0, salta para `RET` (`0x0A6F4384`) |
| **`0x0A6F4380`** | `0a 00 00 14` | `B 0x0A6F43A8` | Se != 0, salta `+0x28` para `0x0A6F43A8` |
| **`0x0A6F4384`** | `c0 03 5f d6` | `RET` | Fim do caminho nulo (`0x14` B antes do `RET`; nenhum store em `[X0]`) |
| **`0x0A6F4388`** | `c8 7b 02 d0` | `ADRP X8, #0x0F66E000` | Início de `UObject::UObject(FVTableHelper&)` (`sub_A6F4388`) |
| **`0x0A6F438C`** | `08 a9 46 f9` | `LDR X8, [X8, #0xD50]` | `[0x0F66ED50]` = `0x0EF8A658` |
| **`0x0A6F4390`** | `09 41 00 91` | `ADD X9, X8, #0x10` | `X9 = 0x0EF8A668` (`vtable_UObject`) |
| **`0x0A6F4394`** | `68 29 fc f0` | `ADRP X8, #0x02C23000` | Página `0x02C23000` |
| **`0x0A6F4398`** | `08 49 2b 91` | `ADD X8, X8, #0xAD2` | `X8 = 0x02C23AD2` (`L"UObject(FVTableHelper& Helper)"`) |
| **`0x0A6F439C`** | `09 00 00 f9` | `STR X9, [X0]` | `this->vtable = 0x0EF8A668` (pertence a `sub_A6F4388`) |
| **`0x0A6F43A0`** | `e0 03 08 aa` | `MOV X0, X8` | `X0 = L"UObject(FVTableHelper& Helper)"` |
| **`0x0A6F43A4`** | `0d b4 f8 17` | `B 0x0A5213D8` | Tail-call `EnsureRetrievingVTablePtrDuringCtor` |
| **`0x0A6F43A8`** | `fd 7b bf a9` | `STP X29, X30, [SP, #-0x10]!` | Bloco frio Fatal de `0x0A6F4314` e `0x0A6F4370` |
| **`0x0A6F43AC`** | `fd 03 00 91` | `MOV X29, SP` | Frame pointer |
| **`0x0A6F43B0`** | `a8 7b 02 90` | `ADRP X8, #0x0F668000` | Página GOT `0x0F668000` |
| **`0x0A6F43B4`** | `08 35 43 f9` | `LDR X8, [X8, #0x668]` | `X8 = [0x0F668668] = 0x10B66CF4` (`&LogUObjectGlobals`) |
| **`0x0A6F43B8`** | `60 35 fc f0` | `ADRP X0, #0x02DA3000` | Página `0x02DA3000` |
| **`0x0A6F43BC`** | `e4 1e fc f0` | `ADRP X4, #0x02AD3000` | Página `0x02AD3000` |
| **`0x0A6F43C0`** | `00 00 1e 91` | `ADD X0, X0, #0x780` | `X0 = 0x02DA3780` (`"Unknown"`) |
| **`0x0A6F43C4`** | `84 38 1a 91` | `ADD X4, X4, #0x68E` | `X4 = 0x02AD368E` (`L"We are currently retrieving VTable ptr..."`) |
| **`0x0A6F43C8`** | `02 11 00 91` | `ADD X2, X8, #0x04` | `X2 = 0x10B66CF8` (`&LogUObjectGlobals.CategoryName`) |
| **`0x0A6F43CC`** | `21 0e 80 52` | `MOV W1, #0x71` | `Line = 113` |
| **`0x0A6F43D0`** | `23 00 80 52` | `MOV W3, #0x01` | `Verbosity = ELogVerbosity::Fatal` (`1`) |
| **`0x0A6F43D4`** | `0c be f7 97` | `BL 0x0A4E3C04` | `FMsg::Logf_Internal` |
| **`0x0A6F43D8`** | `fd 7b c1 a8` | `LDP X29, X30, [SP], #0x10` | Epílogo do bloco Fatal |
| **`0x0A6F43DC`** | `4f 60 f8 17` | `B 0x0A50C518` | Tail-call para sink crítico pós-Fatal |

---

## T4 — Corpo de `sub_A4E3C04` (`FMsg::Logf_Internal`)
- **Grau:** **A**.

| Endereço | Bytes | Instrução | Efeito |
|---|---|---|---|
| `0x0A4E3C04` | `fc 6f ba a9` | `STP X28, X27, [SP, #-0x60]!` | Prólogo |
| `0x0A4E3C20` | `ff 0b 40 d1` | `SUB SP, SP, #0x2C00` | Aloca buffer de formatação + área de `va_list` |
| `0x0A4E3C24` | `ff 43 04 d1` | `SUB SP, SP, #0x110` | Ajuste adicional de frame (`0x2D10` B) |
| `0x0A4E3C28` | `e5 1b 09 a9` | `STP X5, X6, [SP, #0x90]` | **Varargs save GP (`X5`, `X6`)** — `X0..X4` são fixos |
| `0x0A4E3C2C` | `e7 53 00 f9` | `STR X7, [SP, #0xA0]` | **Varargs save GP (`X7`)** |
| `0x0A4E3C30` | `e0 87 00 ad` | `STP Q0, Q1, [SP, #0x10]` | **Varargs save SIMD (`Q0`, `Q1`)** |
| `0x0A4E3C34` | `e2 8f 01 ad` | `STP Q2, Q3, [SP, #0x30]` | **Varargs save SIMD (`Q2`, `Q3`)** |
| `0x0A4E3C38` | `e4 97 02 ad` | `STP Q4, Q5, [SP, #0x50]` | **Varargs save SIMD (`Q4`, `Q5`)** |
| `0x0A4E3C3C` | `e6 9f 03 ad` | `STP Q6, Q7, [SP, #0x70]` | **Varargs save SIMD (`Q6`, `Q7`)** |
| `0x0A4E3CDC` | `b7 fc 01 94` | `BL 0x0A562FB8` | Chama `StaticFailDebug` / `FDebug::AssertFailed` no caminho `Fatal` |
| `0x0A4E3D04` | `68 1e 00 12` | `AND W8, W19, #0xFF` | Aplica `ELogVerbosity::VerbosityMask` |
| `0x0A4E3D14` | `1f 05 00 71` | `CMP W8, #0x01` | Compara com `ELogVerbosity::Fatal` (`1`) |

---

## T5 — Falsificação de `sub_A50C518` e localização de `FUObjectArray::AllocateUObjectIndex` (`0x0A7631B0`)
- **Grau:** **A**.

### 5.1 Listing integral de `sub_A50C518` (`0x0A50C518`–`0x0A50C540`)

| Endereço | Bytes | Instrução | Efeito |
|---|---|---|---|
| `0x0A50C518` | `88 8b 02 b0` | `ADRP X8, #0x0F669000` | Página GOT `0x0F669000` |
| `0x0A50C51C` | `08 69 42 f9` | `LDR X8, [X8, #0x4D0]` | `X8 = [0x0F6694D0] = 0x10B65FA0` (`&GError`) |
| `0x0A50C520` | `73 8b 02 b0` | `ADRP X19, #0x0F669000` | Página GOT `0x0F669000` |
| `0x0A50C524` | `82 2b fc f0` | `ADRP X2, #0x02A7F000` | Página `0x02A7F000` |
| `0x0A50C528` | `69 31 03 d0` | `ADRP X9, #0x10B39000` | Página `.bss` `0x10B39000` |
| `0x0A50C52C` | `00 01 40 f9` | `LDR X0, [X8]` | `X0 = GError` (`FOutputDeviceError*`) |
| `0x0A50C530` | `61 cd 42 f9` | `LDR X1, [X19, #0x598]` | `X1 = [0x0F669598] = 0x10B74118` (`GErrorHist`) |
| `0x0A50C534` | `28 00 80 52` | `MOV W8, #0x01` | `W8 = 1` |
| `0x0A50C538` | `42 b0 11 91` | `ADD X2, X2, #0x46C` | `X2 = 0x02A7F46C` (`L"%s"`) |
| `0x0A50C53C` | `28 21 36 39` | `STRB W8, [X9, #0xD88]` | `GIsCriticalError = 1` (`0x10B39D88`) |
| `0x0A50C540` | `c1 18 01 14` | `B 0x0A552844` | Tail-call `FOutputDevice::Logf(L"%s", GErrorHist)` |

### 5.2 Tabela de falsificação (`sub_A50C518` vs `0x0A7631B0` / `0x0A7639B0`)

| Deve existir | Instrução esperada | Presente em `sub_A50C518`? | Presente no verdadeiro (`0x0A7631B0` / `0x0A7639B0`)? |
|---|---|---|---|
| check de índice | `LDR W8,[X0,#0xC]` + `CMN`/`CMP` | **NÃO** | **SIM** — `0x0A763A34`: `68 0e 40 b9` (`LDR W8, [X19, #0xC]`) |
| pop de `ObjAvailableList` | LDR de global + `LDR W?,[#0x60]` | **NÃO** | **SIM** — `0x0A763234`: `88 62 40 b9` (`LDR W8, [X20, #0x60]`), `0x0A763240`: `89 2e 40 f9` (`LDR X9, [X20, #0x58]`) |
| escrita do índice | `STR W?,[X0,#0xC]` | **NÃO** | **SIM** — `0x0A7634C0`: `75 0e 00 b9` (`STR W21, [X19, #0xC]`) |
| SetFlags | `LDR W8,[X0,#0x8]` / `ORR` / `STR` | **NÃO** | **SIM** — `0x0A7639E0`–`0x0A7639F0` (`LDR/ORR/AND/STR [X19, #0x8]`) e `0x0A7634B4` (`STR W8, [X24, #0x8]`) |
| escrita no item | `STR X0,[item]` | **NÃO** | **SIM** — `0x0A7634B0`: `13 03 00 f9` (`STR X19, [X24]`) |
| `HashObject` | dois globais de hash no fim | **NÃO** | **SIM** — `0x0A763ADC`: `4e bc 00 14` (`B 0x0A792C14` = `HashObject`) |

---

## T6 — P14 (`sub_A6F43E0` e `sub_A451F38`: `FUObjectThreadContext` e `InitializerStack`)
- **Grau:** **A**.
- Em `sub_A6F43E0` (`UObject::CreateDefaultSubobject`, `0x0A6F4444`–`0x0A6F4474`):
  - `0x0A6F4444`: `bd 76 f5 97` (`BL 0x0A451F38` = `TThreadSingleton<FUObjectThreadContext>::Get()`).
  - `0x0A6F4448`: `08 18 40 b9` (`LDR W8, [X0, #0x18]`) → **`+0x18` = `InitializerStack.ArrayNum`**.
  - `0x0A6F4450`: `09 08 40 f9` (`LDR X9, [X0, #0x10]`) → **`+0x10` = `InitializerStack.Data` (`FObjectInitializer**`)**.
- Em `sub_A451F38` (`0x0A451FEC`–`0x0A452018`): o push `TArray` opera nos offsets `+0x30` (`Data`), `+0x38` (`ArrayNum`), `+0x3C` (`ArrayMax`) do container TLS de `FTlsAutoCleanup`.

---

## T7 e T8 — `0x10B66F30` e Descoberta Direta de `GUObjectArray` (`0x10BF7920`)
- **T7:** `0x10B66F30` está em `.bss` (**NÃO LIDO** em processo vivo) e foi provado estaticamente (Grau A) como `GIsRetrievingVTablePtr` (`0` fora de `FVTableHelper`).
- **T8 (Grau A):**
  - `GUObjectArray` (`P - 0x10`) = **`0x10BF7920`** (via GOT `0x0F6685C8`).
  - `ObjObjects` (`P`) = **`0x10BF7930`**.
  - `sizeof(FUObjectItem)` = **`0x18` (24 B)**: `+0x00` `Object` (8 B), `+0x08` `Flags` (4 B), `+0x0C` `ClusterRootIndex` (4 B), `+0x10` `SerialNumber` (4 B), `+0x14` pad (4 B).
  - `UObjectBase` (`0x30` B): `+0x00` `vtable`, `+0x08` `ObjectFlags`, `+0x0C` `InternalIndex`, `+0x10` `ClassPrivate`, `+0x18` `NamePrivate` (12 B), `+0x24` pad (4 B), **`+0x28` `OuterPrivate`** (8 B).

---

# PARTE II — FILA T9–T16 (AOB, `FUOBJECTARRAY`, `FNAMEPOOL`, `GWORLD`, CÂMERA E CLASSES)

## T9 — Validação de 64 índices em processo vivo
- **Grau:** **NÃO LIDO** (`0 / 64` — sessão estática offline sem processo anexado; `adb devices` vazio).
- **Fórmula de validação para processo vivo (Grau A):**
  - `item = *(*(uint64_t**)0x10BF7930 + (idx >> 16)) + (idx & 0xFFFF) * 0x18`
  - `obj = *(uint64_t*)(item + 0x00)`
  - Predicados: `obj != 0`, `*(int32_t*)(obj + 0x0C) == idx`, `*(uint64_t*)(obj + 0x10)` em segmento `rw`.

---

## T10 — AOB de reancoragem (`0x0A7639FC`) e unicidade de GOTs em `.text`
- **Grau:** **A** (varredura em `.text` `0x04BBC000`–`0x0E65A6A0`, `154.658.464` B).

### 10.1 AOB para `0x0A7639F8`–`0x0A763A0B` (`UObjectBase::AddObject`)
- **AOB exata de 8 B (`0x0A7639F8`):**
  - Bytes: `20 78 02 B0 00 E4 42 F9`
  - Máscara: `x x x x x x x x`
  - Unicidade em `.text`: **`1` match** (`0x0A7639F8`).
- **AOB independente de posição de 44 B (`0x0A7639E0`–`0x0A763A0B`):**
  - Bytes: `68 0A 40 B9 ?? ?? 38 36 94 02 02 32 08 79 18 12 68 0A 00 B9 ?? ?? 10 37 ?? ?? ?? ?? ?? ?? 4? F9 E1 03 13 AA E2 03 1F 2A ?? ?? ?? 97`
  - Máscara: `xxxx??xxxxxxxxxxxxxx??xx??????x?xxxxxxxx???x`
  - Unicidade em `.text`: **`1` match** (`0x0A7639E0`).

### 10.2 Contagem de pares `ADRP+LDR` em `.text` por slot GOT

| Slot GOT | Valor estático | Alvo | Matches em `.text` | Endereços / Âncora |
|---|---|---|---|---|
| `0x0F6685C8` | `0x10BF7920` | `GUObjectArray` | **`15.073`** | `0x0A7639F8` (`20 78 02 b0 00 e4 42 f9`), `0x0A763A2C` |
| `0x0F66ED50` | `0x0EF8A658` | `vtable_UObject - 0x10` | **`6`** | `0x0A6F4338`, `0x0A6F4388`, `0x0A7028A8`, `0x0A70329C`, `0x0A7034A8`, `0x0A703730` |
| `0x0F66ED58` | `0x10B66F30` | `GIsRetrievingVTablePtr` | **`2`** | `0x0A6F4348`, `0x0A6F4370` |
| `0x0F668668` | `0x10B66CF4` | `LogUObjectGlobals` | **`38`** | `0x0A6F43B0` (`c8 7b 02 90 08 35 43 f9`) |
| `0x0F6694D0` | `0x10B65FA0` | `GError` | **`10`** | `0x0A50C518`, `0x052A44E4`, `0x073D0444`, `0x073D2758`, `0x073E040C`, `0x0BB99DDC`, `0x0BB9A008`, `0x0BB9A330`, `0x0BBAA8E0`, `0x0BBC8770` |
| `0x0F669598` | `0x10B74118` | `GErrorHist` | **`53`** | `0x0A50C520` + `0x0A50C530` |

---

## T11 — Mapa completo de `FUObjectArray` (`0x10BF7920`) provado por `0x0A7631B0` e `0x0A761A54`
- **Grau:** **A**.

| Offset em `GUObjectArray` | Endereço `.bss` | Tipo e Campo | Instrução de Prova |
|---|---|---|---|
| `+0x00` | `0x10BF7920` | `int32 ObjFirstGCIndex` | `0x0A7631A0`: `1f 00 00 b9` (`STR WZR, [X0]`); `0x0A763218`: `88 02 40 b9` (`LDR W8, [X20]`) |
| `+0x04` | `0x10BF7924` | `int32 ObjLastNonGCIndex` | `0x0A763204`: `88 06 40 b9` (`LDR W8, [X20, #0x4]`); `0x0A763210`: `95 06 00 b9` (`STR W21, [X20, #0x4]`) |
| `+0x08` | `0x10BF7928` | `int32 MaxObjectsNotConsideredByGC` | `0x0A7631F8`: `80 0a 40 b9` (`LDR W0, [X20, #0x8]`); `0x0A763404`: `88 0a 00 b9` (`STR W8, [X20, #0x8]`) |
| `+0x0C` | `0x10BF792C` | `bool OpenForDisregardForGC` | `0x0A7631F0`: `88 32 40 39` (`LDRB W8, [X20, #0xC]`); `0x0A763490`: `88 32 40 39` (`LDRB W8, [X20, #0xC]`) |
| `+0x10` | `0x10BF7930` | `FUObjectItem** ObjObjects.Objects` | `0x0A7632BC`: `9b 0a 40 f9` (`LDR X27, [X20, #0x10]`); `0x0A763420`: `89 0a 40 f9` (`LDR X9, [X20, #0x10]`) |
| `+0x18` | `0x10BF7938` | `FUObjectItem* ObjObjects.PreAllocatedObjects` | `0x0A761AB4`: `94 05 00 36` (`TBZ W20, #0, 0x0A761B64`) + `0x0A761B10`: `60 06 00 f9` (`STR X0, [X19, #0x8]`) |
| `+0x20` | `0x10BF7940` | `int32 ObjObjects.MaxElements` | `0x0A7633D0`: `89 22 40 b9` (`LDR W9, [X20, #0x20]`) |
| `+0x24` | `0x10BF7944` | `int32 ObjObjects.NumElements` | `0x0A763324`: `95 26 40 b9` (`LDR W21, [X20, #0x24]`); `0x0A7633DC`: `88 26 00 b9` (`STR W8, [X20, #0x24]`) |
| `+0x28` | `0x10BF7948` | `int32 ObjObjects.MaxChunks` | `0x0A761A94`: `08 18 00 b9` (`STR W8, [X0, #0x18]`, onde `X0 = GUObjectArray + 0x10`) |
| `+0x2C` | `0x10BF794C` | `int32 ObjObjects.NumChunks` | `0x0A763284`: `9a 2e 40 b9` (`LDR W26, [X20, #0x2C]`); `0x0A7632B0`: `9a 2e 00 b9` (`STR W26, [X20, #0x2C]`) |
| `+0x30` | `0x10BF7950` | `FCriticalSection ObjObjectsCritical` (`0x28` B) | `0x0A7631D8`: `16 c0 00 91` (`ADD X22, X0, #0x30`) + `0x0A7631EC`: `BL 0x0E657320` (`pthread_mutex_lock`) |
| `+0x58` | `0x10BF7978` | `TArray<int32> ObjAvailableList` | `0x0A763234`: `88 62 40 b9` (`LDR W8, [X20, #0x60]`); `0x0A763240`: `89 2e 40 f9` (`LDR X9, [X20, #0x58]`) |
| `+0x68` | `0x10BF7988` | `TArray<FUObjectCreateListener*> UObjectCreateListeners` | `0x0A7634DC`: `88 72 40 b9` (`LDR W8, [X20, #0x70]`); `0x0A7634E8`: `LDR X8, [X20, #0x68]` |

---

## T12 — `FNamePool` (`GNamePool = 0x10B7C100`) e `FNameEntry` (`WITH_CASE_PRESERVING_NAME = 1`)
- **Grau:** **A** (refuta o RVA `0x0F697C78` de `SDK_Metadata.json`).
- **Endereços e Layout provados em `FName::ToString(FString&)` (`0x0A59F808`) e `FNamePool::FNamePool()` (`0x0A59ABDC`):**
  - `bNamePoolInitialized`: **`0x10BCF0C0`** (`0x0A59F818`–`0x0A59F81C`: `ADRP X8, #0x10BCF000; LDRB W8, [X8, #0xC0]`).
  - `GNamePool` (`NamePoolData`): **`0x10B7C100`** (`0x0A59F82C`–`0x0A59F830`: `ADRP X21, #0x10B7C000; ADD X21, X21, #0x100`).
  - `FNameEntryAllocator`:
    - `+0x00..+0x37`: `FRWLock Lock`
    - `+0x38`: `uint32 CurrentBlock`
    - `+0x3C`: `uint32 CurrentByteCursor`
    - **`+0x40` (`0x10B7C140`)**: `uint8* Blocks[8192]` (`FNameMaxBlocks = 8192`, `FNameBlockOffsetBits = 16`, **`Stride = 4`**).
  - **Fórmula de resolução de `UObjectBase::NamePrivate` (`+0x18`):**
    - `DisplayIndex = *(uint32_t*)(Obj + 0x1C)` (`+0x18` = `ComparisonIndex`, `+0x1C` = `DisplayIndex`, `+0x20` = `Number`, lidos via `LDP W8, W9, [X20, #0x4]` em `0x0A59F848`).
    - `BlockIdx = DisplayIndex >> 16` (`LSR X10, X8, #13; AND X10, X10, #0x3FFF8` em `0x0A59F84C`–`0x0A59F854`).
    - `ByteOffset = (DisplayIndex & 0xFFFF) * 4` (`LSL W8, W8, #2; AND X8, X8, #0x3FFFC` em `0x0A59F85C`–`0x0A59F864`).
    - `FNameEntry* Entry = *(uint8_t**)(0x10B7C100 + 0x40 + BlockIdx * 8) + ByteOffset` (`0x0A59F860`–`0x0A59F868`).
    - Layout de `FNameEntry`:
      - `+0x00`: `uint32 ComparisonId` (`4` B)
      - `+0x04`: `uint16 Header` (`bIsWide = Header & 1`, `Len = Header >> 1`, lido por `LDRH W8, [X21, #0x4]; LSR W1, W8, #1` em `0x0A59F86C`–`0x0A59F870`)
      - `+0x06`: `char AnsiName[Len]` ou `wchar_t WideName[Len]` (lido por `ADD X1, X0, #0x6` em `0x0A59DC80`).
  - **Prova por objeto conhecido:** `EName(0)` (`DisplayIndex = 0, Number = 0`) é registrado em `0x0A59AD84`–`0x0A59ADB0` a partir do literal `0x02DC7DFE` (`"None"`, `4E 6F 6E 65 00`, `Len = 4`) e resolve para **`"None"`**.

---

## T13, T14 e T15 — `GWorld`, `ULevel::Actors`, Cadeia de Câmera, `FMinimalViewInfo` e Projeção
- **Grau:** **A** (exceto valor runtime de `PersistentLevel` em `.bss` = **NÃO LIDO**).

### 13.1 Globais de Mundo e Cadeia completa até a Câmera

| Etapa / Símbolo | Endereço / Offset | Tipo | Instrução de Prova |
|---|---|---|---|
| `GWorld` (`UWorldProxy`) | **`0x10D69998`** (GOT **`0x0F668440`**) | `UWorld*` | `0x04BC9678`: `ADRP X8, #0x0F668000` + `0x04BC967C`: `LDR X8, [X8, #0x440]` + `0x04BC9680`: `LDR X21, [X8]` |
| `GEngine` | **`0x10D65798`** (GOT **`0x0F6682A8`**) | `UEngine*` | `0x0C1BFDDC`: `ADRP X8, #0x0F668000` + `0x0C1BFDE0`: `LDR X8, [X8, #0x2A8]` |
| `UWorld::StaticClass()` | **`0x0CA4C76C`** → **`0x10D953E8`** | `UClass*` | `0x0CA4C77C`: `ADRP X8, #0x10D95000` + `0x0CA4C780`: `LDR X0, [X8, #0x3E8]` |
| `UWorld::PersistentLevel` | **`+0x0038`** | `ULevel*` | `0x0C78A6C0`: `00 1c 40 f9` (`LDR X0, [X0, #0x38]; RET`) |
| `ULevel::Actors.Data` | **`+0x00A0`** | `AActor**` | `0x0C78A624`: `a8 52 40 f9` (`LDR X8, [X21, #0xA0]`) |
| `ULevel::Actors.ArrayNum` | **`+0x00A8`** | `int32` | `0x0C78A610`: `a8 aa 40 b9` (`LDR W8, [X21, #0xA8]`) |
| `UWorld::OwningGameInstance` | **`+0x0410`** | `UGameInstance*` | `0x0C1BFDFC`: `00 08 42 f9` (`LDR X0, [X0, #0x410]`) |
| `UGameInstance::LocalPlayers` | **`+0x0040`** | `TArray<ULocalPlayer*>` | `FClassParams` de `UGameInstance` (`rva = 0x0C85FE90`) |
| `UPlayer::PlayerController` | **`+0x0038`** | `APlayerController*` | `FClassParams` de `UPlayer` (`rva = 0x0C99FE40`; inverso `Player` em `+0x02B0` lido em `0x0C4C7E84`) |
| `APlayerController::AcknowledgedPawn` | **`+0x02B8`** | `APawn*` | `FClassParams` de `APlayerController` (`0x0F3B84D8`) |
| `APlayerController::PlayerCameraManager` | **`+0x02D0`** | `APlayerCameraManager*` | `0x0C1C019C`: `00 68 41 f9` (`LDR X0, [X0, #0x2D0]`) |
| `APlayerCameraManager::CameraCachePrivate` | **`+0x1AF0`** (`POV` em **`+0x1B00`**) | `FCameraCacheEntry` (`0x600` B) | `FClassParams` de `APlayerCameraManager` (`0x0F3B0910`; `CameraCache` em `+0x02A0`, `ViewTarget` em `+0x0EA0`) |

### 13.2 Layout de `FMinimalViewInfo` (`0x0F2841E8`, `SizeOf = 0x05F0`)

| Offset no `POV` | Offset em `APlayerCameraManager` | Tipo | Campo |
|---|---|---|---|
| `+0x0000` | `+0x1B00` | `FVector` (`12 B`) | `Location` |
| `+0x000C` | `+0x1B0C` | `FRotator` (`12 B`) | `Rotation` |
| `+0x0018` | `+0x1B18` | `float` | `FOV` |
| `+0x001C` | `+0x1B1C` | `float` | `DesiredFOV` |
| `+0x0020` | `+0x1B20` | `float` | `OrthoWidth` |
| `+0x0024` | `+0x1B24` | `float` | `OrthoNearClipPlane` |
| `+0x0028` | `+0x1B28` | `float` | `OrthoFarClipPlane` |
| `+0x002C` | `+0x1B2C` | `float` | `AspectRatio` |
| `+0x0030` | `+0x1B30` | `uint8` (bit 0 = `0x01`, bit 1 = `0x02`) | `bConstrainAspectRatio` (`0x0C81479C`), `bUseFieldOfViewForLOD` (`0x0C8147AC`) |
| `+0x0031` | `+0x1B31` | `uint8` (`ECameraProjectionMode::Type`) | `ProjectionMode` |
| `+0x0034` | `+0x1B34` | `float` | `PostProcessBlendWeight` |
| `+0x0040` | `+0x1B40` | `FPostProcessSettings` (`0x0560 B`) | `PostProcessSettings` |
| `+0x05A0` | `+0x20A0` | `FVector2D` (`8 B`) | `OffCenterProjectionOffset` |

---

## T16 — Classes de Gameplay (`Survivor`/`Camper`, `Killer`/`Slasher`, `Camera`) e Offsets Críticos
- **Grau:** **A**.

| Alvo | Classe | Offset | Tipo / Máscara | Prova |
|---|---|---|---|---|
| `RootComponent` | `AActor` (`0x0C7ABD84`, `size = 0x0230`) | **`+0x0140`** | `USceneComponent*` | `RelativeLocation` = `+0x0134`, `RelativeRotation` = `+0x0140`, `RelativeScale3D` = `+0x014C` |
| `bIsVisible` (`bVisible`) | `USceneComponent` (`0x0C9E2054`, `size = 0x0220`) | **`+0x0164`** | `uint8` bit 6 (**mask `0x40`**) / `uint16` mask `0x0040` | `SetBitFunc` `0x0C9E1FC4`: `LDRH W8, [X0, #0x164]; ORR W8, W8, #0x40; STRH W8, [X0, #0x164]` |
| `bHiddenInGame` | `USceneComponent` (`0x0C9E2054`, `size = 0x0220`) | **`+0x0165`** (`+0x0164`) | `uint8` bit 4 (**mask `0x10`**) / `uint16` mask `0x1000` | `SetBitFunc` `0x0C9E2024`: `LDRH W8, [X0, #0x164]; ORR W8, W8, #0x1000; STRH W8, [X0, #0x164]` |
| `Mesh` | `ACharacter` (`0x0C81DD30`, `size = 0x04D0`) | **`+0x0290`** | `USkeletalMeshComponent*` | `CachedBoneSpaceTransforms` = `+0x07E0`, `CachedComponentSpaceTransforms` = `+0x07F0` |
| `Camera` / `CameraBoom` | `ADBDPlayer` (`0x0780E190`, `size = 0x14D0`) | **`+0x05E8`** / **`+0x0780`** | `UCameraComponent*` / `USpringArmComponent*` | `CurrentCamperState` = `+0x0A68`, `_cameraHandlerComponent` = `+0x0C00`, `_associatedPlayerStateCache` = `+0x1458` |
| `HP` / Health (Survivor) | `ACamperPlayer` (`0x0761E5A8`, `size = 0x1B90`) | **`+0x1838`**<br>**`+0x1840`**<br>**`+0x1848`** | `UCamperHealthComponent* _healthComponent`<br>`UChargeableComponent* _hpSlot01`<br>`UChargeableComponent* _hpSlot02` | Em `UCamperHealthComponent` (`0x07609400`): `_inInjuredBleedOut` = `+0x0170`, `_currentHealthStateCount` = `+0x01EC`, `_numHealthyHeals` = `+0x01F4`, `_healFromInjuredChargeable` = `+0x0250` |
| Killer / Slasher | `ASlasherPlayer` (`0x07BA58FC`, `size = 0x1B40`) | **`+0x1808`**<br>**`+0x1A28`**<br>**`+0x1A70`** | `USlasherTREmitterComponent* _terrorRadiusEmitter`<br>`ACamperPlayer* _carriedCamper`<br>`ACamperPlayer* _survivorBeingKilled` | `DefaultSlasherPower` = `+0x1524`, `KillerAbilities` = `+0x1558`, `_slasherStunnableComponent` = `+0x1AE0` |
| Camera Manager (DBD) | `ADBDPlayerCameraManager` (`0x07823E18`, `size = 0x2840`) | **`+0x2820`**<br>**`+0x2824`** | `float NormalFOV`<br>`float TargetingFOV` | Herda `CameraCachePrivate` (`+0x1AF0`) e `POV` (`+0x1B00`) de `APlayerCameraManager` (`0x0C9A2D90`) |

---

## LISTA CONSOLIDADA DE PENDÊNCIAS RUNTIME (PROCESSO VIVO)

- **T7**: Leitura runtime de `GIsRetrievingVTablePtr` (`0x10B66F30`) pendente de anexação a processo vivo.
- **T9**: Amostragem runtime de 64 itens de `GUObjectArray` (`0x10BF7930`, stride `0x18`) pendente de anexação a processo vivo.
- **T13 (runtime)**: Confirmação runtime de `*(*(uint64_t*)0x10D69998 + 0x38) != 0` (`GWorld->PersistentLevel`) pendente de anexação a processo vivo.

