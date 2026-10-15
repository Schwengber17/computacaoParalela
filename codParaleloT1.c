#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <omp.h>

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

// Imprime a disposição das rainhas no tabuleiro
void imprimirTabuleiro(int tabuleiro[], int n, int solucao) {

    printf("--- Solução %d ---\n", solucao);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            printf(tabuleiro[i] == j ? " Q " : " . ");
        }
        printf("\n");
    }

    printf("\n");
}

// Função recursiva de backtracking
void resolverNRainhas(
    int tabuleiro[],
    int linha,
    int n,
    int *totalSolucoes,
    bool exibir
) {

    // ========================================================
    // CASO BASE
    // ========================================================
    if (linha == n) {

        (*totalSolucoes)++;

        if (exibir) {

            /*
             * A impressão utiliza stdout, que é compartilhado
             * entre as threads. Por isso, protegemos somente
             * a impressão.
             */
            #pragma omp critical
            {
                imprimirTabuleiro(
                    tabuleiro,
                    n,
                    *totalSolucoes
                );
            }
        }

        return;
    }

    // ========================================================
    // PRIMEIRO NÍVEL:
    //
    // Somente o primeiro nível da árvore é paralelizado.
    //
    // Cada thread recebe uma possibilidade para a primeira
    // rainha e executa todo o backtracking daquele ramo.
    // ========================================================
    if (linha == 0) {

        /*
         * Cada iteração representa uma posição possível
         * para a primeira rainha.
         *
         * schedule(dynamic, 1):
         * uma thread recebe uma coluna por vez.
         */
        #pragma omp parallel for schedule(dynamic, 1)
        for (int coluna = 0; coluna < n; coluna++) {

            /*
             * Cada thread/iteração possui seu próprio
             * tabuleiro.
             *
             * Isso evita que várias threads alterem
             * simultaneamente o mesmo tabuleiro.
             */
            int *tabuleiroLocal = malloc(n * sizeof(int));

            if (tabuleiroLocal == NULL) {
                continue;
            }

            // Copia o estado inicial do tabuleiro
            for (int i = 0; i < n; i++) {
                tabuleiroLocal[i] = tabuleiro[i];
            }

            // Coloca a primeira rainha
            tabuleiroLocal[0] = coluna;

            /*
             * Contador LOCAL desta thread.
             *
             * Não é compartilhado.
             */
            int solucoesLocais = 0;

            // Continua o backtracking normalmente
            resolverNRainhas(
                tabuleiroLocal,
                1,
                n,
                &solucoesLocais,
                exibir
            );

            /*
             * Guarda o resultado da thread.
             *
             * Cada posição pertence a uma thread.
             */
            int id = omp_get_thread_num();

            /*
             * Essa variável é configurada no main.
             * O vetor é usado somente para registrar
             * o resultado individual de cada thread.
             */
            extern int *solucoesPorThread;

            solucoesPorThread[id] += solucoesLocais;

            free(tabuleiroLocal);
        }

        return;
    }

    // ========================================================
    // BACKTRACKING ORIGINAL
    //
    // A partir daqui continua exatamente a lógica
    // sequencial original.
    // ========================================================
    for (int coluna = 0; coluna < n; coluna++) {

        if (ehSeguro(tabuleiro, linha, coluna)) {

            tabuleiro[linha] = coluna;

            resolverNRainhas(
                tabuleiro,
                linha + 1,
                n,
                totalSolucoes,
                exibir
            );
        }
    }
}

// ============================================================
// Variável utilizada para guardar as soluções de cada thread
// ============================================================
int *solucoesPorThread = NULL;


// ============================================================
// MAIN
// ============================================================
int main() {

    int n;
    int opcaoExibir = 0;
    int numThreads;

    printf("Digite o valor de N (ex: 4, 8, 12): ");

    if (scanf("%d", &n) != 1 || n <= 0) {
        printf("Entrada inválida.\n");
        return 1;
    }

    printf("Digite o número de threads: ");

    if (scanf("%d", &numThreads) != 1 || numThreads <= 0) {
        printf("Número de threads inválido.\n");
        return 1;
    }

    printf(
        "Deseja imprimir os tabuleiros no terminal? "
        "(1 - Sim / 0 - Não [recomendado para medir performance]): "
    );

    scanf("%d", &opcaoExibir);

    bool exibir = (opcaoExibir == 1);

    // Define o número de threads do OpenMP
    omp_set_num_threads(numThreads);

    // ========================================================
    // Vetor para armazenar as soluções de cada thread
    // ========================================================

    solucoesPorThread = calloc(
        numThreads,
        sizeof(int)
    );

    if (solucoesPorThread == NULL) {
        printf("Erro ao alocar memória.\n");
        return 1;
    }

    // ========================================================
    // Tabuleiro inicial
    // ========================================================

    int *tabuleiro = malloc(n * sizeof(int));

    if (tabuleiro == NULL) {
        printf("Erro ao alocar memória.\n");
        free(solucoesPorThread);
        return 1;
    }

    // ========================================================
    // EXECUÇÃO
    // ========================================================

    printf("\n========================================\n");
    printf("Processando N = %d...\n", n);
    printf("Threads solicitadas: %d\n", numThreads);
    printf("========================================\n");

    double inicio = omp_get_wtime();

    /*
     * O resultado total também pode ser obtido por reduction.
     *
     * Porém, como queremos manter o resolverNRainhas original
     * e registrar as soluções por thread, o total pode ser
     * calculado depois somando os contadores individuais.
     */
    int totalSolucoes = 0;

    resolverNRainhas(
        tabuleiro,
        0,
        n,
        &totalSolucoes,
        exibir
    );

    double fim = omp_get_wtime();

    // ========================================================
    // RESULTADOS
    // ========================================================

    printf("\n========================================\n");

    printf("Soluções encontradas por thread:\n\n");

    for (int i = 0; i < numThreads; i++) {

        printf(
            "Thread %d: %d soluções\n",
            i,
            solucoesPorThread[i]
        );

        totalSolucoes += solucoesPorThread[i];
    }

    printf("\n----------------------------------------\n");

    printf(
        "Total de soluções: %d\n",
        totalSolucoes
    );

    printf(
        "Tempo de execução: %.6f segundos\n",
        fim - inicio
    );

    printf(
        "Threads utilizadas: %d\n",
        numThreads
    );

    printf("========================================\n");

    // ========================================================
    // LIBERA MEMÓRIA
    // ========================================================

    free(tabuleiro);
    free(solucoesPorThread);

    return 0;
}