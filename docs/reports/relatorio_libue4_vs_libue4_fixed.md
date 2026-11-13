# Relatório Técnico: Comparação `binaries/libUE4.so` vs `binaries/libUE4_fixed.so`

**Data:** 2026-11-13  
**Escopo:** Verificação de identidade binária, análise seção a seção do ELF, prova de equivalência via realocações AArch64 (`.rela.dyn` / `.rela.plt`) e impacto sobre `tools/re_scripts/dump/dump_offsets.py`, `tools/re_scripts/dump/check_drift.py` e `docs/offsets/offsets.json`.

---

## 1. Conclusão Executiva

`binaries/libUE4.so` e `binaries/libUE4_fixed.so` correspondem à **exata mesma compilação/build** do binário (`263.651.496` bytes / `0x0FB700A8`).  
Eles não são byte-a-byte idênticos em disco porque `libUE4_fixed.so` é o próprio `libUE4.so` com as realocações dinâmicas (`.rela.dyn` e `.rela.plt`) pré-aplicadas *in-place* nas seções de dados e ponteiros.

Ao aplicar programaticamente `.rela.dyn` (`R_AARCH64_RELATIVE = 1027` e realocações simbólicas `257` / `1025` / `1026`) e `.rela.plt` sobre uma cópia em memória de `binaries/libUE4.so`, o buffer resultante coincide em **100% dos 263.651.496 bytes** com `binaries/libUE4_fixed.so` (**0 bytes de diferença**).

---

## 2. Identidade e Cabeçalhos ELF

| Arquivo | Tamanho (bytes) | SHA-256 | Cabeçalho ELF |
| :--- | :--- | :--- | :--- |
| `binaries/libUE4.so` | `263,651,496` | `1ca51f84065cf096ad51c1f346684bf97d2dfe0afd2f1b64b98018562311c38a` | `e_phoff=0x40` (`phnum=10`), `e_shoff=0xfb6f9e8` (`shnum=27`, `shstrndx=26`) |
| `binaries/libUE4_fixed.so` | `263,651,496` | `d278cc176fad45622d97414e6b574b9219fa2e5810d7ec8ead09cfd2b0cd511f` | `e_phoff=0x40` (`phnum=10`), `e_shoff=0xfb6f9e8` (`shnum=27`, `shstrndx=26`) |

Layout ELF comum reportado por `elf_layout.parse_elf_layout`:

- `.text`: `79413248 .. 241529564` (`0x04bbc000 .. 0x0e6572dc`)
- `.plt`: `241529568 .. 241542816` (`0x0e6572e0 .. 0x0e65a6a0`)
- `.data.rel.ro`: `241545216 .. 258354056` (`0x0e65b000 .. 0x0f662b88`)
- `.data`: `258465792 .. 263648120` (`0x0f67e000 .. 0x0fb6f378`)
- `.bss`: `263648120 .. 284836264` (`bss_va = 263648128 .. 284836272`)
- `.eh_frame_hdr`: `off = 58054160` (`0x0375d610`), `size = 4453228` (`0x0043f36c`), `fde_count = 556652`

---

## 3. Comparação por Seção ELF (`e_shnum = 27`)

Na faixa comum `[0 .. 0xfb700a8)`, foram detectadas `1.603.575` faixas de bytes diferentes — todas concentradas exclusivamente nas **5 seções de dados/ponteiros realocáveis** (`[17]`, `[18]`, `[19]`, `[21]`, `[23]`). As outras **22 seções** são byte-a-byte idênticas:

| Índice | Seção | Offset | Tamanho | Idêntico? |
| :--- | :--- | :--- | :--- | :--- |
| `[ 0]` | *(null)* | `0x00000000` | `0x00000000` | `True` |
| `[ 1]` | `.note.android.ident` | `0x00000270` | `0x00000098` | `True` |
| `[ 2]` | `.note.gnu.build-id` | `0x00000308` | `0x00000024` | `True` |
| `[ 3]` | `.dynsym` | `0x00000330` | `0x00124020` | `True` |
| `[ 4]` | `.gnu.version` | `0x00124350` | `0x00018558` | `True` |
| `[ 5]` | `.gnu.version_r` | `0x0013c8a8` | `0x00000060` | `True` |
| `[ 6]` | `.gnu.hash` | `0x0013c908` | `0x0005bca4` | `True` |
| `[ 7]` | `.hash` | `0x001985ac` | `0x00061568` | `True` |
| `[ 8]` | `.dynstr` | `0x001f9b14` | `0x002fe3b9` | `True` |
| `[ 9]` | `.rela.dyn` | `0x004f7ed0` | `0x023bcae8` | `True` |
| `[10]` | `.rela.plt` | `0x028b49b8` | `0x00004d70` | `True` |
| `[11]` | `.rodata` | `0x028b9740` | `0x00e7c686` | `True` |
| `[12]` | `.gcc_except_table` | `0x03735dc8` | `0x00027848` | `True` |
| `[13]` | `.eh_frame_hdr` | `0x0375d610` | `0x0043f36c` | `True` |
| `[14]` | `.eh_frame` | `0x03b9c980` | `0x0101ec0c` | `True` |
| `[15]` | `.text` | `0x04bbc000` | `0x09a9b2dc` | `True` |
| `[16]` | `.plt` | `0x0e6572e0` | `0x000033c0` | `True` |
| `[17]` | `.data.rel.ro` | `0x0e65b000` | `0x01007b88` | **`False`** |
| `[18]` | `.fini_array` | `0x0f662b88` | `0x00000010` | **`False`** |
| `[19]` | `.init_array` | `0x0f662b98` | `0x000054b0` | **`False`** |
| `[20]` | `.dynamic` | `0x0f668048` | `0x00000250` | `True` |
| `[21]` | `.got` | `0x0f668298` | `0x000135c8` | **`False`** |
| `[22]` | `.got.plt` | `0x0f67b860` | `0x000019e8` | `True` |
| `[23]` | `.data` | `0x0f67e000` | `0x004f1378` | **`False`** |
| `[24]` | `.bss` | `0x0fb6f378` | `0x01434e30` | `True` (`NOBITS`) |
| `[25]` | `.comment` | `0x0fb6f378` | `0x00000566` | `True` |
| `[26]` | `.shstrtab` | `0x0fb6f8de` | `0x00000104` | `True` |

---

## 4. Prova de Equivalência via Realocações `.rela.dyn` / `.rela.plt`

Em binários ELF AArch64 (`ET_DYN` com formato `RELA`), os campos de ponteiros em `.data.rel.ro`, `.init_array`, `.fini_array`, `.got` e `.data` permanecem zerados em disco (`0x0000000000000000`), enquanto o valor real de cada ponteiro é armazenado na tabela de realocações (`.rela.dyn` / `.rela.plt`), tipicamente via `R_AARCH64_RELATIVE (1027)` com o endereço alvo em `r_addend` ou via realocação simbólica (`257`, `1025`, `1026`) somando `st_value + r_addend`.

Exemplo observado no início de `.data.rel.ro` (`0x0e65b000`):

- `[0xe65b018..0xe65b01c)`: `libUE4.so = 00000000` vs `libUE4_fixed.so = e879bc04` (`0x04bc79e8`)
- `[0xe65b020..0xe65b024)`: `libUE4.so = 00000000` vs `libUE4_fixed.so = f483bc04` (`0x04bc83f4`)
- `[0xe65b028..0xe65b02c)`: `libUE4.so = 00000000` vs `libUE4_fixed.so = fc4ebd04` (`0x04bd4efc`)

Aplicando em memória as entradas de `.rela.dyn` (`off=0x004f7ed0`, `size=0x023bcae8`) e `.rela.plt` (`off=0x028b49b8`, `size=0x00004d70`) sobre `binaries/libUE4.so`:

```text
Identical after applying .rela.dyn/.rela.plt: True
```

Isso confirma que nenhuma instrução em `.text`, dado constante em `.rodata`, tabela `.eh_frame_hdr` ou string foi modificada em `libUE4_fixed.so`.

---

## 5. Impacto no Pipeline de Offsets e Auditoria Atual

### 5.1 Por que `dump_offsets.py` exige `libUE4_fixed.so`
O dumper percorre `.data.rel.ro` e `.data` lendo ponteiros diretos das estruturas de reflexão (`FClassParams`, `FPropertyParams`, `FNativeFunctionLookup` e `vtable`s). No `libUE4.so` original sem realocação aplicada, esses ponteiros em disco são `0`, impedindo a descoberta das tabelas de reflexão sem antes aplicar `.rela.dyn`.

### 5.2 Comportamento do `check_drift.py` contra ambos os arquivos
- **Contra `binaries/libUE4_fixed.so`:**
  ```text
  OK: 3958 members verified against binaries\libUE4_fixed.so (changed=0, missing=0, unverified_strict=0, unverified_inferred=0).
  ```
  *(Exit code `0`)*

- **Contra `binaries/libUE4.so`:**
  ```text
  CHANGED: binary SHA-256 expected d278cc176fad45622d97414e6b574b9219fa2e5810d7ec8ead09cfd2b0cd511f, got 1ca51f84065cf096ad51c1f346684bf97d2dfe0afd2f1b64b98018562311c38a
  DRIFT DETECTED: checked=3958 changed=0 missing=0 unverified_strict=0 unverified_inferred=0
  ```
  *(Exit code `1` apenas pela diferença de SHA-256 no cabeçalho do dump).*  
  Todos os `3958` membros passam sem erro porque os prólogos em `.text`, tamanhos em `.eh_frame_hdr`, strings em `.rodata` e o offset escalar `+0x24` (`Offset_Internal`, campo `u32` não realocado) de `FPropertyParams` são idênticos nos dois binários.

### 5.3 Resumo da Auditoria Atual de `docs/offsets/offsets.json` (`3958` registros nos domínios `skillcheck`, `player`, `world`)

- **Por `confidence`:** `string=1910`, `xref=1220`, `inferred=828`
- **Por `size_source`:** `fde=1571`, `reflection=1559`, `thunk=427`, `inferred=401`
- **Métodos (`UFunction*`):** `UFunction=787` (`767` únicos por `(class, member, rva)`), `UFunction_Thunk=427`, `UFunction_Exec=351`
- **Helpers contaminantes em `UFunction`:** `0` ocorrências para `0x0A44AE90` (`_ZdlPv`), `0x0A6669B0`, `0x0A6B4C54` e `0x0A648DE4`.

