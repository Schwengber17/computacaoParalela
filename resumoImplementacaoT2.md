# T2 - N-Rainhas com MPI (coordenador/trabalhador)

Registro da implementação e das decisões. Arquivos:

| Arquivo | Conteúdo |
|---|---|
| `codSequencialBacktracking.c` | Referência sequencial (T1), base do `T_seq` |
| `codParaleloT1.c` | Versão OpenMP (T1) |
| `codMpiT2.c` | MPI, versão **básica** (3 mensagens por tarefa) |
| `codMpiT2Otimizado.c` | MPI, versão **otimizada** (2 mensagens por tarefa) |

## 1. Problema e modelagem

- **Problema:** contar as soluções de N-Rainhas por backtracking (o mesmo do T1).
- **Eixo paralelizado:** a árvore de busca. Cada tarefa é um **prefixo válido de `prof` rainhas** (linhas `0..prof-1` já posicionadas). O trabalhador completa o backtracking das linhas `prof..N-1`.
- **Saco de tarefas:** matriz `m x (prof+1)` de `int`, contígua, em ordem lexicográfica dos prefixos.
  - As `prof` primeiras colunas são o prefixo.
  - A última coluna é a **contagem de soluções**, preenchida pelo trabalhador.
  - Assim, a "linha processada" que volta ao mestre é o próprio vetor da tarefa mais o resultado, como no enunciado.
- **Ordem preservada:** o mestre guarda `tarefaDoTrabalhador[rank]` (qual slot cada trabalhador pegou). O resultado volta ao slot de origem, não ao de chegada.
- **Total:** soma da última coluna do saco ao final.
- **Desbalanceamento natural:** o custo dos prefixos é muito desigual (prefixos com rainhas nas bordas têm subárvores maiores, e alguns prefixos quase não têm soluções). Por isso o saco dinâmico é adequado e há o que analisar no balanceamento.

## 2. Decisões tomadas

| Decisão | Escolha | Motivo |
|---|---|---|
| Granularidade | `prof` por argumento, padrão 3 | Mais tarefas melhoram o balanceamento, mas custam mais mensagens. É um parâmetro a explorar na análise |
| Entrada | `argv`: `N [profundidade]` 
| Validação de argumentos | Todos os ranks validam e saem juntos | Evita processos pendurados em `MPI_Recv` |
| `prof` | Limitado a `[1, N]`

## 3. Protocolo de mensagens

Tags: `TAG_PEDIDO`, `TAG_TAREFA`, `TAG_RESULTADO`, `TAG_FIM`. A mensagem de fim tem 0 elementos.

### Versão básica (`codMpiT2.c`)

```
trabalhador                         mestre
  PEDIDO (vazio)        ----->      recebe de MPI_ANY_SOURCE
                        <-----      TAREFA (linha do saco)  ou  FIM
  processa
  RESULTADO (linha)     ----->      recebe em buffer, copia para saco[tarefaDoTrabalhador[origem]]
  PEDIDO ...
```

- Iniciativa dos trabalhadores, como no enunciado.
- O mestre usa um buffer temporário e depois copia para o slot.
- Mensagens: `3m + 2(np-1)` (m tarefas, np-1 trabalhadores).

### Versão otimizada (`codMpiT2Otimizado.c`)

```
mestre                              trabalhador
  TAREFA inicial (sem pedido) ---->  processa
  MPI_Probe(ANY_SOURCE, RESULTADO)
  MPI_Recv direto em saco[slot] <--  RESULTADO (linha)
  TAREFA seguinte  ou  FIM    ---->  ...
```

- Distribuição inicial automática: no começo ninguém tem trabalho, então não há por que esperar pedido.
- O resultado devolvido já vale como pedido de nova tarefa.
- `MPI_Probe` informa quem respondeu, e o `MPI_Recv` grava direto no saco, sem buffer intermediário nem cópia.
- Mensagens: `2m + (np-1)`.
- Se há mais trabalhadores do que tarefas, os excedentes recebem `FIM` já na distribuição inicial.

Observação sobre ordem: o MPI garante que mensagens de um mesmo remetente não se ultrapassam. Na versão básica, isso assegura que o último `RESULTADO` de um trabalhador chega antes do `PEDIDO` seguinte, então o `FIM` só é enviado depois de o resultado estar guardado.

## 4. Medições

- `MPI_Barrier` e depois `MPI_Wtime`. O tempo vai do início da geração do saco até o mestre receber o último resultado e enviar o último `FIM`.
- A **geração do saco** (sequencial, no mestre) está **dentro** do tempo medido.
- As estatísticas por trabalhador (tarefas e tempo de cálculo) são reunidas com `MPI_Gather` **depois** do tempo medido.
- O relatório impresso traz: tarefas e tempo de cálculo por trabalhador, mín/média/máx, **desbalanceamento (máx/média)**, mensagens tratadas pelo mestre e conferência do total para N ≤ 18.
- `T_seq` para o speedup: usar `codSequencialBacktracking.c` (ou `np = 1` neste programa), **sempre na mesma máquina** que os testes paralelos.

Fórmulas para o relatório:

- Speedup forte: `S(p) = T_seq / T_par(p)`.
- Eficiência: `E(p) = S(p) / p`. Vale reportar também `S(p) / (p-1)`, já que o mestre não calcula.

## 5. Como compilar e executar

```bash
mpicc -O2 codMpiT2.c -o codMpiT2.exe
mpicc -O2 codMpiT2Otimizado.c -o codMpiT2Otimizado.exe

# local
mpirun -np 4 ./codMpiT2.exe 12 3

# cluster (atlantica): 2 nós exclusivos
srun --exclusive -N 2 -n 16 ./codMpiT2.exe 14 3
srun --exclusive -N 2 -n 32 ./codMpiT2Otimizado.exe 14 3
```

Valores de conferência: N=8 → 92, N=10 → 724, N=12 → 14200, N=14 → 365596.


## 6. Pontos para a análise do relatório

- **Hyper-Threading:** cada nó tem 8 núcleos e 16 threads. Com `-N 2`, 16 processos podem ocupar um processo por núcleo físico, enquanto 32 usam HT. Comparar o speedup entre esses dois pontos e discutir o ganho marginal.
- **Mestre ocioso:** com `p` processos há `p-1` trabalhadores. Além disso, o `MPI_Recv`/`MPI_Probe` costuma fazer espera ativa, ocupando um núcleo (ou uma thread de HT) do nó do mestre.
- **Granularidade (`prof`):** testar 2, 3 e 4. Poucas tarefas desbalanceiam. Muitas tarefas aumentam as mensagens, e o custo da mensagem pesa mais na versão básica.
- **Balanceamento:** usar o desbalanceamento (máx/média) e a distribuição de tarefas por trabalhador. Tarefas com custos diferentes terminam em tempos diferentes, e o pedido sob demanda compensa isso.
- **Básica x otimizada:** comparar tempo e número de mensagens para o mesmo N, `prof` e `np`.

### Escalabilidade fraca (sugestão a calibrar)

O trabalho de N-Rainhas cresce cerca de 5 a 6 vezes por incremento de N (razões das soluções conhecidas: 4,96; 6,2; 6,5). Como N é inteiro, só dá para aproximar trabalho proporcional ao número de trabalhadores:

| Processos | Trabalhadores | Fator de trabalho desejado | N sugerido |
|---|---|---|---|
| 2 | 1 | 1x | N₀ = 13 |
| 7 | 6 | ~6x | N₀ + 1 = 14 |
| 32 | 31 | ~30x | N₀ + 2 = 15 |

- O ponto de 16 processos (15 trabalhadores, ~15x) fica entre N=14 e N=15.
- **Calibrar com `T_seq` medido** na máquina de teste. Se necessário, ajustar N₀ para o ponto de 32 processos não ficar longo demais.
- Como o fator real não é exato, calcular a eficiência fraca usando o `T_seq` medido de cada N, e não o fator teórico.

## 7. Estado da verificação

- Falta compilar e conferir no WSL (com `openmpi`) ou no cluster, rodando N=8, 10, 12 e 14 com `np` = 1, 2, 3 e 16, e `prof` = 1, 3 e `N`.
