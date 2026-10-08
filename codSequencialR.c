#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

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
void resolverNRainhas(int tabuleiro[], int linha, int n, int *totalSolucoes, bool exibir) {
    if (linha == n) {
        (*totalSolucoes)++;
        if (exibir) {
            imprimirTabuleiro(tabuleiro, n, *totalSolucoes);
        }
        return;
    }

    for (int coluna = 0; coluna < n; coluna++) {
        if (ehSeguro(tabuleiro, linha, coluna)) {
            tabuleiro[linha] = coluna;
            resolverNRainhas(tabuleiro, linha + 1, n, totalSolucoes, exibir);
        }
    }
}

int main() {
    int n;
    int opcaoExibir = 0;

    printf("Digite o valor de N (ex: 4, 8, 12, 14): ");
    if (scanf("%d", &n) != 1 || n <= 0) {
        printf("Entrada inválida.\n");
        return 1;
    }

    printf("Deseja imprimir os tabuleiros no terminal? (1 - Sim / 0 - Não [recomendado para medir performance]): ");
    if (scanf("%d", &opcaoExibir) != 1) {
        printf("Entrada inválida.\n");
        return 1;
    }

    int *tabuleiro = (int *)malloc(n * sizeof(int));
    if (tabuleiro == NULL) {
        printf("Erro ao alocar memória.\n");
        return 1;
    }

    int totalSolucoes = 0;
    bool exibir = (opcaoExibir == 1);

    printf("\nProcessando N = %d...\n", n);

    // 1. MARCA O TEMPO INICIAL DE RELÓGIO (Wall-Clock Time)
    struct timespec inicio, fim;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    // Executa a resolução via Backtracking
    resolverNRainhas(tabuleiro, 0, n, &totalSolucoes, exibir);

    // 2. MARCA O TEMPO FINAL
    clock_gettime(CLOCK_MONOTONIC, &fim);

    // 3. CALCULA O TEMPO DE EXECUÇÃO EM SEGUNDOS E MILISSEGUNDOS
    double tempoSegundos = (fim.tv_sec - inicio.tv_sec) + 
                          (fim.tv_nsec - inicio.tv_nsec) / 1e9;
    double tempoMs = tempoSegundos * 1000.0;

    printf("\n========================================\n");
    printf("Total de soluções encontradas: %d\n", totalSolucoes);
    printf("Tempo de execução: %.3f ms (%.6f s)\n", tempoMs, tempoSegundos);
    printf("========================================\n");

    free(tabuleiro);
    return 0;
}