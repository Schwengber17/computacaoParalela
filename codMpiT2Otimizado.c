/*
 * N-Rainhas com MPI - modelo coordenador/trabalhador (saco de tarefas)
 * VERSAO OTIMIZADA (2 mensagens por tarefa)
 *
 * Compilar: mpicc -O2 codMpiT2Otimizado.c -o codMpiT2Otimizado.exe
 * Executar: mpirun -np P ./codMpiT2Otimizado.exe N [profundidade]
 *           srun --exclusive -N 2 -n 32 ./codMpiT2Otimizado.exe N [profundidade]
 *
 * Saco de trabalho: matriz m x (prof+1). Cada linha e uma tarefa: as
 * primeiras 'prof' posições são as colunas das rainhas já posicionadas
 * nas linhas 0..prof-1 e a última posição recebe o número de soluções
 * encontradas pelo trabalhador a partir daquele prefixo.
 *
 * Diferenças em relação a versão básica (codMpiT2.c):
 *   1. Distribuição inicial automática: o mestre envia uma tarefa a cada
 *      trabalhador sem esperar pedido (no início ninguém tem trabalho).
 *   2. Sem mensagem de pedido: a devolução do resultado já é o pedido de
 *      nova tarefa; o mestre responde com a próxima tarefa ou TAG_FIM.
 *   3. MPI_Probe identifica quem respondeu antes de ler a mensagem, e o
 *      MPI_Recv grava direto na linha do saco, sem buffer intermediário.
 *
 * Protocolo:
 *   mestre -> trabalhador : TAG_TAREFA (linha do saco) ou TAG_FIM (sem dados)
 *   trabalhador -> mestre : TAG_RESULTADO (linha processada)
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define MESTRE 0
#define PROFUNDIDADE_PADRAO 3

#define TAG_TAREFA    2
#define TAG_RESULTADO 3
#define TAG_FIM       4

// Número de soluções conhecidas para N = 0..18, usado só para conferência
#define N_MAX_CONHECIDO 18
static const long long solucoesConhecidas[N_MAX_CONHECIDO + 1] = {
    1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200, 73712,
    365596, 2279184, 14772512, 95815104, 666090624
};

static void *alocar(size_t bytes) {
    void *p = malloc(bytes > 0 ? bytes : 1);
    if (p == NULL) {
        fprintf(stderr, "Erro ao alocar memória.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    return p;
}

// Verifica se é seguro posicionar a rainha em (linha, coluna)
bool ehSeguro(int tabuleiro[], int linha, int coluna) {
    for (int i = 0; i < linha; i++) {
        // Conflito na mesma coluna
        if (tabuleiro[i] == coluna) {
            return false;
        }
        // Conflito nas diagonais
        if (abs(tabuleiro[i] - coluna) == abs(i - linha)) {
            return false;
        }
    }
    return true;
}

// Backtracking sequencial a partir de 'linha'; retorna o número de soluções
long long contarSolucoes(int tabuleiro[], int linha, int n) {
    if (linha == n) {
        return 1;
    }

    long long total = 0;
    for (int coluna = 0; coluna < n; coluna++) {
        if (ehSeguro(tabuleiro, linha, coluna)) {
            tabuleiro[linha] = coluna;
            total += contarSolucoes(tabuleiro, linha + 1, n);
        }
    }
    return total;
}

/*
 * Enumera, em ordem lexicográfica, todos os prefixos válidos de 'prof' rainhas.
 * Com saco == NULL apenas conta; caso contrário grava cada prefixo numa linha.
 */
void gerarPrefixos(int tabuleiro[], int linha, int prof, int n, int *saco, int *qtd) {
    if (linha == prof) {
        if (saco != NULL) {
            int *tarefa = &saco[(size_t)(*qtd) * (prof + 1)];
            for (int i = 0; i < prof; i++) {
                tarefa[i] = tabuleiro[i];
            }
            tarefa[prof] = 0;
        }
        (*qtd)++;
        return;
    }

    for (int coluna = 0; coluna < n; coluna++) {
        if (ehSeguro(tabuleiro, linha, coluna)) {
            tabuleiro[linha] = coluna;
            gerarPrefixos(tabuleiro, linha + 1, prof, n, saco, qtd);
        }
    }
}

// Resolve uma tarefa: completa o backtracking e grava a contagem na última posição
void processarTarefa(int tarefa[], int prof, int n, int tabuleiro[]) {
    for (int i = 0; i < prof; i++) {
        tabuleiro[i] = tarefa[i];
    }
    tarefa[prof] = (int)contarSolucoes(tabuleiro, prof, n);
}

/*
 * Coordenador: gerencia o saco e guarda os resultados direto nas linhas.
 * Retorna o total de soluções; *qtdTarefas e *mensagens alimentam o relatório.
 */
long long mestre(int n, int prof, int np, int *qtdTarefas, long long *mensagens) {
    int tam = prof + 1;
    int *tabuleiro = alocar(n * sizeof(int));

    // Monta o saco de trabalho (duas passadas: contar e preencher)
    int m = 0;
    gerarPrefixos(tabuleiro, 0, prof, n, NULL, &m);
    int *saco = alocar((size_t)m * tam * sizeof(int));
    int geradas = 0;
    gerarPrefixos(tabuleiro, 0, prof, n, saco, &geradas);

    // Qual tarefa (slot do saco) cada trabalhador está processando
    int *tarefaDoTrabalhador = alocar(np * sizeof(int));
    for (int i = 0; i < np; i++) {
        tarefaDoTrabalhador[i] = -1;
    }

    int proxima = 0;
    int recebidos = 0;
    int ativos = np - 1;

    // Distribuição inicial: uma tarefa para cada trabalhador, sem esperar pedido
    for (int w = 1; w < np; w++) {
        if (proxima < m) {
            tarefaDoTrabalhador[w] = proxima;
            MPI_Send(&saco[(size_t)proxima * tam], tam, MPI_INT, w, TAG_TAREFA, MPI_COMM_WORLD);
            proxima++;
        } else {
            // Mais trabalhadores do que tarefas
            MPI_Send(NULL, 0, MPI_INT, w, TAG_FIM, MPI_COMM_WORLD);
            ativos--;
        }
        (*mensagens)++;
    }

    MPI_Status status;
    while (ativos > 0) {
        // Descobre quem terminou primeiro sem ler a mensagem
        MPI_Probe(MPI_ANY_SOURCE, TAG_RESULTADO, MPI_COMM_WORLD, &status);
        int origem = status.MPI_SOURCE;

        // Recebe direto no slot que esse trabalhador estava processando
        MPI_Recv(&saco[(size_t)tarefaDoTrabalhador[origem] * tam], tam, MPI_INT, origem,
                 TAG_RESULTADO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        recebidos++;
        (*mensagens)++;

        // A devolução do resultado vale como pedido: responde com mais trabalho ou fim
        if (proxima < m) {
            tarefaDoTrabalhador[origem] = proxima;
            MPI_Send(&saco[(size_t)proxima * tam], tam, MPI_INT, origem, TAG_TAREFA, MPI_COMM_WORLD);
            proxima++;
        } else {
            MPI_Send(NULL, 0, MPI_INT, origem, TAG_FIM, MPI_COMM_WORLD);
            ativos--;
        }
        (*mensagens)++;
    }

    if (recebidos != m) {
        fprintf(stderr, "Erro: %d resultados recebidos para %d tarefas.\n", recebidos, m);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    long long total = 0;
    for (int i = 0; i < m; i++) {
        total += saco[(size_t)i * tam + prof];
    }

    *qtdTarefas = m;
    free(tarefaDoTrabalhador);
    free(saco);
    free(tabuleiro);
    return total;
}

// Trabalhador: recebe tarefa, processa e devolve a linha até receber TAG_FIM
void trabalhador(int n, int prof, int *tarefasFeitas, double *tempoCalculo) {
    int tam = prof + 1;
    int *tarefa = alocar(tam * sizeof(int));
    int *tabuleiro = alocar(n * sizeof(int));
    MPI_Status status;

    while (true) {
        MPI_Recv(tarefa, tam, MPI_INT, MESTRE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_FIM) {
            break;
        }

        double t0 = MPI_Wtime();
        processarTarefa(tarefa, prof, n, tabuleiro);
        *tempoCalculo += MPI_Wtime() - t0;
        (*tarefasFeitas)++;

        MPI_Send(tarefa, tam, MPI_INT, MESTRE, TAG_RESULTADO, MPI_COMM_WORLD);
    }

    free(tabuleiro);
    free(tarefa);
}

void imprimirRelatorio(int n, int prof, int np, int m, long long total, double tempo,
                       long long mensagens, int tarefasPorRank[], double calculoPorRank[]) {
    printf("\n========================================\n");
    printf("N-Rainhas MPI - versão OTIMIZADA\n");
    printf("N = %d | profundidade = %d | tarefas no saco = %d\n", n, prof, m);
    printf("Processos: %d (1 coordenador + %d trabalhadores)\n", np, np - 1);
    printf("========================================\n");

    if (np > 1) {
        double soma = 0.0, maximo = 0.0, minimo = calculoPorRank[1];
        printf("Carga por trabalhador:\n");
        for (int r = 1; r < np; r++) {
            printf("  Rank %2d: %6d tarefas | cálculo %.6f s\n", r, tarefasPorRank[r], calculoPorRank[r]);
            soma += calculoPorRank[r];
            if (calculoPorRank[r] > maximo) maximo = calculoPorRank[r];
            if (calculoPorRank[r] < minimo) minimo = calculoPorRank[r];
        }
        double media = soma / (np - 1);
        printf("  Cálculo: mín %.6f s | média %.6f s | máx %.6f s\n", minimo, media, maximo);
        printf("  Desbalanceamento (máx/média): %.3f\n", media > 0.0 ? maximo / media : 1.0);
        printf("Mensagens tratadas pelo coordenador: %lld\n", mensagens);
        printf("----------------------------------------\n");
    }

    printf("Total de soluções: %lld", total);
    if (n <= N_MAX_CONHECIDO) {
        if (total == solucoesConhecidas[n]) {
            printf(" (conferido)");
        } else {
            printf(" (DIVERGE do esperado: %lld)", solucoesConhecidas[n]);
        }
    }
    printf("\nTempo de execução: %.6f segundos\n", tempo);
    printf("========================================\n");
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, np;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &np);

    // Todos os processos validam os mesmos argumentos, então saem juntos
    int n = 0;
    int prof = PROFUNDIDADE_PADRAO;
    if (argc < 2 || (n = atoi(argv[1])) <= 0 || (argc >= 3 && (prof = atoi(argv[2])) <= 0)) {
        if (rank == MESTRE) {
            fprintf(stderr, "Uso: %s N [profundidade]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }
    if (prof > n) {
        prof = n;
    }

    int tarefasFeitas = 0;
    double tempoCalculo = 0.0;
    int m = 0;
    long long total = 0;
    long long mensagens = 0;
    double fim = 0.0;

    MPI_Barrier(MPI_COMM_WORLD);
    double inicio = MPI_Wtime();

    if (np == 1) {
        // Sem trabalhadores: o mestre resolve tudo (referência sequencial)
        int *tabuleiro = alocar(n * sizeof(int));
        total = contarSolucoes(tabuleiro, 0, n);
        free(tabuleiro);
        fim = MPI_Wtime();
    } else if (rank == MESTRE) {
        total = mestre(n, prof, np, &m, &mensagens);
        fim = MPI_Wtime();
    } else {
        trabalhador(n, prof, &tarefasFeitas, &tempoCalculo);
    }

    // Estatísticas por processo para a análise de balanceamento (fora do tempo medido)
    int *tarefasPorRank = NULL;
    double *calculoPorRank = NULL;
    if (rank == MESTRE) {
        tarefasPorRank = alocar(np * sizeof(int));
        calculoPorRank = alocar(np * sizeof(double));
    }
    MPI_Gather(&tarefasFeitas, 1, MPI_INT, tarefasPorRank, 1, MPI_INT, MESTRE, MPI_COMM_WORLD);
    MPI_Gather(&tempoCalculo, 1, MPI_DOUBLE, calculoPorRank, 1, MPI_DOUBLE, MESTRE, MPI_COMM_WORLD);

    if (rank == MESTRE) {
        imprimirRelatorio(n, prof, np, m, total, fim - inicio, mensagens, tarefasPorRank, calculoPorRank);
        free(tarefasPorRank);
        free(calculoPorRank);
    }

    MPI_Finalize();
    return 0;
}
