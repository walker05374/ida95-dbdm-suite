# Fluxo de Reconciliação de Movimento no `UCharacterMovementComponent` (Unreal Engine 4)

## Visão Geral

Na Unreal Engine 4, o `UCharacterMovementComponent` implementa uma arquitetura de movimento **preditiva no cliente** e **autoritativa no servidor** (*Client-Side Prediction & Server Reconciliation*).  
O cliente executa seus próprios inputs imediatamente para manter responsividade, registra cada movimento em uma fila de histórico (`SavedMoves`) e envia os dados compactados ao servidor via RPC. O servidor reexecuta o movimento de forma autoritativa, compara o resultado com o estado reportado pelo cliente e, em caso de divergência, envia uma correção que força o cliente a ajustar seu estado e reprocessar (*replay*) os movimentos pendentes.

---

## Ordem das Funções no Caminho de Reconciliação (Passos 1 a 9)

### 1. `ReplicateMoveToServer` (Cliente)
- Cria e preenche uma instância de `FSavedMove_Character` com o estado atual de entrada (aceleração, rotação de controle, flags de movimento, timestamp e estado inicial).
- Armazena o movimento na lista `SavedMoves` dentro de `FNetworkPredictionData_Client_Character`.
- Executa a simulação preditiva localmente chamando `PerformMovement`, atualizando posição, velocidade e estado físico imediato do personagem no cliente.
- Grava o estado pós-movimento no `FSavedMove_Character` e decide (com base no intervalo de envio ou mudanças críticas de estado) se deve despachar imediatamente para o servidor.

### 2. `CallServerMove` (Cliente)
- Serializa os parâmetros do movimento atual (e opcionalmente do movimento anterior não confirmado, usando `ServerMoveDual` ou `ServerMoveOld` para resiliência a perda de pacotes).
- Despacha o RPC (`ServerMove` / `ServerMoveDual`) ao servidor contendo:
  - `TimeStamp`
  - `InAccel` (aceleração)
  - `ClientLoc` (posição resultante predita pelo cliente)
  - `CompressedMoveFlags` (flags compactadas, como salto, agachar, etc.)
  - `ClientRoll` e `View` (rotação/orientação compactada)
  - Modo de movimento compacto (`ClientMovementMode`) e referência de base (`ClientBase`).

### 3. `ServerMove_Implementation` (Servidor)
- Recebe o RPC enviado pelo cliente no servidor autoritativo.
- Calcula o intervalo de tempo validado via `GetServerMoveDeltaTime`, garantindo proteção contra variação indevida de *timestamps* ou atrasos anômalos.
- Descompacta as flags recebidas por meio de `UpdateFromCompressedFlags`.
- Encaminha os parâmetros validados para `MoveAutonomous` a fim de reproduzir o passo de simulação no servidor.

### 4. `MoveAutonomous` e `PerformMovement` (Servidor)
- `MoveAutonomous` aplica a aceleração, a rotação e o estado de entrada recebidos para aquele `DeltaTime`.
- Delega a execução física completa para `PerformMovement` (que por sua vez aciona a física específica do modo atual, como `PhysWalking`, `PhysFalling`, `PhysFlying`, etc.).
- Atualiza a posição, velocidade e `MovementMode` autoritativos do personagem no servidor.

### 5. `ServerMoveHandleClientError` (Servidor)
- Executada logo após a simulação autoritativa no servidor.
- Compara o estado final calculado no servidor (posição, velocidade, modo de movimento e base) com o estado reportado pelo cliente (`ClientLoc`, `ClientMovementMode`, `ClientBase`).
- Aciona a verificação formal de erro para determinar se uma correção ao cliente é necessária.

### 6. `ServerCheckClientError` e `ServerExceedsAllowablePositionError` (Servidor)
- Calcula a diferença vetorial de posição (`LocDiff`) entre a posição autoritativa no servidor e `ClientLoc`.
- Verifica se houve divergência de modo de movimento (`bMovementModeDiffers`).
- Avalia a condição de disparo de correção:
  ```cpp
  ServerExceedsAllowablePositionError(LocDiff) || bMovementModeDiffers
  ```
  - **Importante:** Uma divergência no modo de movimento (`bMovementModeDiffers`) dispara a correção por si só, independentemente de o erro de posição estar abaixo do limite tolerado.
- Se a condição for verdadeira, registra os dados de ajuste em `PendingAdjustment` dentro de `FNetworkPredictionData_Server_Character`.

### 7. `SendClientAdjustment` (Servidor)
- Chamada durante o ciclo de replicação/envio do servidor quando há `PendingAdjustment` pendente ou confirmação de movimento válido:
  - **`ClientAckGoodMove`**: enviada quando o movimento do cliente foi validado sem erro significativo, permitindo que o cliente descarte o `FSavedMove_Character` correspondente.
  - **`ClientAdjustPosition` / `ClientVeryShortAdjustPosition`**: enviadas quando há divergência detectada, transportando a posição autoritativa (`NewLoc`), velocidade (`NewVel`), base (`NewBase`), `TimeStamp` e `ServerMovementMode`.

### 8. `ClientAdjustPosition_Implementation` (Cliente)
Ao receber o RPC de ajuste do servidor, o cliente aplica o estado autoritativo na seguinte ordem:
1. **Aplicação do modo de movimento:** O modo não é aplicado diretamente via `SetMovementMode`, mas sim por `ApplyNetworkMovementMode(ServerMovementMode)`, que desempacota o byte compacto enviado pela rede (`UnpackNetworkMovementMode`) e então chama `SetMovementMode`.
2. **Reposicionamento e atualização física:**
   - Reposiciona o componente atualizado via `MoveUpdatedComponent` para `NewLoc`.
   - Atualiza `Velocity` para `NewVel`.
   - Atualiza a base de movimento via `SetBase` (caso aplicável) e sincroniza a velocidade do componente com `UpdateComponentVelocity()`.
   - Marca a flag de atualização pendente (`bUpdatePosition = true`) em `FNetworkPredictionData_Client_Character` para sinalizar a necessidade de replay.
3. **Registro do estado base para o replay:** Ao final da função, grava `LastUpdateLocation` e `LastUpdateVelocity` com os valores corrigidos, que servirão de referência para o cálculo de erro e ponto de partida no replay do passo seguinte.

### 9. `ClientUpdatePositionAfterServerUpdate` (Cliente)
- Executada no próximo tick do cliente quando `bUpdatePosition` está marcado.
- Descarta todos os `SavedMoves` cujo `TimeStamp` seja menor ou igual ao último `TimeStamp` reconhecido/ajustado pelo servidor.
- Partindo do estado autoritativo recém-aplicado (usando `LastUpdateLocation` e `LastUpdateVelocity` como base), reexecuta sequencialmente todos os `SavedMoves` ainda pendentes em `SavedMoves` via `MoveAutonomous` / `PerformMovement`.
- Ao concluir o replay, aciona `SmoothCorrection` para suavizar visualmente a transição da malha (*mesh*) entre a posição antiga predita e a nova posição reconciliada, evitando saltos abruptos na tela.

---

## Boas Práticas de Validação Autoritativa

- **Validação de `DeltaTime`:** O cálculo em `GetServerMoveDeltaTime` limita e valida o tempo acumulado entre RPCs para evitar aceleração artificial (*speed hacking*) ou saltos temporais inválidos.
- **Consistência de `MovementMode`:** Validar `bMovementModeDiffers` em conjunto com o erro posicional garante que o cliente não permaneça em um modo físico não autorizado (por exemplo, voando ou nadando indevidamente) mesmo quando a diferença imediata de posição ainda é pequena.
- **Limites de Erro Configuráveis:** A tolerância avaliada em `ServerExceedsAllowablePositionError` equilibra responsividade em redes com latência/jitter contra a precisão da autoridade do servidor.
- **Reconciliação Determinística:** O uso de `ApplyNetworkMovementMode` antes do replay em `ClientUpdatePositionAfterServerUpdate` assegura que todos os movimentos pendentes sejam reprocessados sob as mesmas regras físicas do servidor.

