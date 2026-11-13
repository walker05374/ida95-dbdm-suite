# Relatório Técnico: Varredura de Assinaturas de Bytes (AOB Scan) Otimizada em C++20 (ARM64)

## 1. Visão Geral
Este documento descreve a arquitetura e o funcionamento lógico da rotina de **Pattern Scanning (Array of Bytes / AOB Scan)** projetada em **C++20** para ambientes embarcados e binários **ELF64 ARM64 (AArch64)** compilados com as flags `-O3`, `-fno-exceptions` e `-fno-rtti`.

O objetivo do módulo é localizar assinaturas binárias estáticas (contendo bytes exatos e coringas/wildcards `?` ou `??`) dentro de seções de memória contíguas (como `.text` ou `.rodata`) com custo zero de alocação dinâmica (heap-free) e aceleração vetorizada via instruções SIMD da biblioteca padrão (`std::memchr`).

---

## 2. Requisitos de Projeto e Restrições de Compilação
- **Compatibilidade com `-fno-exceptions`:** Nenhuma exceção C++ é lançada. Erros de formatação de assinatura ou falhas de busca são reportados por valor através de `std::optional` e retorno de `nullptr`.
- **Compatibilidade com `-fno-rtti`:** Sem uso de `dynamic_cast` ou `typeid`.
- **Zero Alocações Dinâmicas (Heap-Free):** Uso de `std::array` parametrizado por template (`MaxCapacity`) e `std::span<const std::uint8_t>` para evitar `std::vector` ou `std::string`.
- **Avaliação em Tempo de Compilação (`constexpr`):** O parser de assinaturas (`CompiledPattern::compile`) pode ser executado em tempo de compilação, embutindo a estrutura já processada diretamente na seção `.rodata` do binário final.

---

## 3. Implementação em C++20

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <optional>
#include <string_view>
#include <array>

namespace ue4::introspection {

struct PatternByte {
    std::uint8_t value{0};
    bool is_wildcard{false};
};

/**
 * Representa um padrão de bytes pré-processado em buffer estático.
 * Evita alocações dinâmicas (std::vector) e exceções.
 */
template <std::size_t MaxCapacity = 128>
struct CompiledPattern {
    std::array<PatternByte, MaxCapacity> bytes{};
    std::size_t length{0};
    std::size_t first_concrete_idx{0};
    bool has_concrete_byte{false};

    [[nodiscard]] static constexpr std::optional<CompiledPattern> compile(std::string_view signature) noexcept {
        CompiledPattern out{};
        std::size_t i = 0;

        auto hex_to_nibble = [](char c) constexpr noexcept -> std::optional<std::uint8_t> {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
            return std::nullopt;
        };

        while (i < signature.size()) {
            // Ignora espaços em branco
            while (i < signature.size() && signature[i] == ' ') {
                ++i;
            }
            if (i >= signature.size()) break;
            if (out.length >= MaxCapacity) return std::nullopt;

            if (signature[i] == '?') {
                out.bytes[out.length++] = PatternByte{0, true};
                ++i;
                if (i < signature.size() && signature[i] == '?') {
                    ++i; // Suporta tanto "?" quanto "??"
                }
            } else {
                if (i + 1 >= signature.size()) return std::nullopt;
                const auto hi = hex_to_nibble(signature[i]);
                const auto lo = hex_to_nibble(signature[i + 1]);
                if (!hi || !lo) return std::nullopt;

                if (!out.has_concrete_byte) {
                    out.first_concrete_idx = out.length;
                    out.has_concrete_byte = true;
                }

                out.bytes[out.length++] = PatternByte{
                    static_cast<std::uint8_t>((*hi << 4) | *lo),
                    false
                };
                i += 2;
            }
        }

        if (out.length == 0) return std::nullopt;
        return out;
    }
};

/**
 * Realiza a busca da assinatura em uma região de memória contígua (ex: seção .text).
 * Retorna o ponteiro para o início da correspondência ou nullptr caso não encontre.
 */
template <std::size_t MaxCapacity = 128>
[[nodiscard]] const std::uint8_t* find_pattern(
    std::span<const std::uint8_t> region,
    const CompiledPattern<MaxCapacity>& pattern
) noexcept {
    if (pattern.length == 0 || region.size() < pattern.length) [[unlikely]] {
        return nullptr;
    }

    // Se a assinatura inteira for composta apenas por wildcards, retorna o início da região
    if (!pattern.has_concrete_byte) [[unlikely]] {
        return region.data();
    }

    const std::size_t anchor_offset = pattern.first_concrete_idx;
    const std::uint8_t anchor_byte  = pattern.bytes[anchor_offset].value;
    const std::size_t max_start_pos = region.size() - pattern.length;

    std::size_t cursor = 0;

    while (cursor <= max_start_pos) {
        // Quantidade de posições válidas restantes onde a âncora ainda pode ocorrer
        const std::size_t remaining_search_len = (max_start_pos - cursor) + 1;

        // Salto rápido vetorizado (NEON/ASIMD na libc do ARM64) até a próxima ocorrência do byte âncora
        const void* hit = std::memchr(
            region.data() + cursor + anchor_offset,
            anchor_byte,
            remaining_search_len
        );

        if (!hit) {
            return nullptr;
        }

        // Recua do endereço da âncora para obter o início candidato da assinatura completa
        const auto* candidate_base = static_cast<const std::uint8_t*>(hit) - anchor_offset;
        bool full_match = true;

        // Verifica os demais bytes concretos após a âncora
        for (std::size_t i = anchor_offset + 1; i < pattern.length; ++i) {
            if (!pattern.bytes[i].is_wildcard && candidate_base[i] != pattern.bytes[i].value) {
                full_match = false;
                break;
            }
        }

        if (full_match) [[likely]] {
            return candidate_base;
        }

        // Avança o cursor para o próximo byte após o candidato atual
        cursor = static_cast<std::size_t>(candidate_base - region.data()) + 1;
    }

    return nullptr;
}

} // namespace ue4::introspection
```

---

## 4. Análise Detalhada do Funcionamento Lógico

### 4.1. Pré-processamento sem Heap (`CompiledPattern::compile`)
1. **Conversão Lexical:** A função percorre um `std::string_view` contendo bytes hexadecimais separados por espaço (ex: `"08 00 ? 90 08 ? 00 91"`) e converte cada par de caracteres hexadecimais em um `std::uint8_t` através de deslocamento de bits (`(*hi << 4) | *lo`).
2. **Suporte a Wildcards:** Tokens `?` ou `??` são marcados com a flag `is_wildcard = true`, permitindo ignorar deslocamentos variáveis de instruções (como imediatos de `ADRP`, `ADD`, `LDR` ou `BL` em ARM64).
3. **Identificação de Âncora (`first_concrete_idx`):** O compilador registra o índice do primeiro byte não-coringa. Isso garante que mesmo padrões iniciados por coringas (ex: `"? ? 00 90"`) possam utilizar a busca acelerada por `std::memchr`.

### 4.2. Ancoragem e Aceleração via SIMD (`std::memchr`)
1. Em vez de executar um loop ingênuo $O(N \times M)$ testando byte a byte desde o início da seção `.text`, o algoritmo delega a varredura primária para `std::memchr` buscando apenas o byte concreto localizado em `anchor_offset`.
2. Em sistemas **Linux/Android ARM64**, `std::memchr` é implementado em assembly utilizando registradores vetoriais **NEON/ASIMD de 128 bits** (`ld1`, `cmeq`, `umaxp`), processando de 16 a 64 bytes por iteração e saltando rapidamente por regiões que não contêm o byte âncora.

### 4.3. Validação da Janela Candidata e Controle de Limites
1. **Cálculo de Base Candidata:** Quando `std::memchr` retorna um ponteiro `hit`, o início real da janela candidata é obtido recuando `anchor_offset` bytes (`candidate_base = hit - anchor_offset`).
2. **Otimização da Comparação Interna:** Como todos os índices anteriores a `anchor_offset` são coringas e o índice `anchor_offset` já foi confirmado por `std::memchr`, o laço de verificação começa diretamente em `i = anchor_offset + 1`.
3. **Prevenção de Leitura Fora dos Limites (Out-of-Bounds):** O limite superior `max_start_pos = region.size() - pattern.length` garante que `candidate_base + i` nunca ultrapasse o final do `std::span` fornecido.

