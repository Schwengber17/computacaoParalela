# Tutorial de Acesso e Execução de Código no Cluster

Este guia rápido contém as instruções necessárias para acessar os servidores **Sparta** e **Atlântica**, compilar programas em C utilizando OpenMP e executá-los em nós exclusivos através do gerenciador Slurm.

---

## 1. Conexão SSH

### 1.1. Servidor Sparta
Acesse o terminal (PowerShell, Git Bash ou Linux) e execute:

```bash
ssh luana.sostisso@sparta.pucrs.br
```
* **Senha:** Utilize a sua senha do e-mail institucional.

---

### 1.2. Servidor Atlântica
Para conectar-se ao servidor Atlântica:

```bash
ssh cp32014@atlantica.lad.pucrs.br
```
* **Username:** `cp32014`
* **Senha:** `IJmupb2w6gE3qLBtbhpH`
Ijmupb2w6gE3qLBtbhpH

---

## 2. Compilação do Código C (OpenMP)

Para compilar arquivos que utilizam diretivas de paralelismo do OpenMP, utilize a flag `-fopenmp` junto ao `gcc`:

```bash
gcc -fopenmp primos.c -o primos.exe
```

---

## 3. Execução no Cluster via Slurm (`srun`)

Para submeter a execução do seu binário em um nó exclusivo de processamento através do gerador de tarefas Slurm:

```bash
srun --exclusive -N 1 ./primos.exe
```

> **Nota:** Certifique-se de substituir `./primos.exe` pelo nome do arquivo executável que você definiu durante a compilação.