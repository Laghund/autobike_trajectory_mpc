#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "qpSWIFT/include/qpSWIFT.h"

typedef struct
{
    double A[4];            // discretized, row-major order
    double B[2];            // discretized, row-major order
    uint32_t N;             // prediction horizon
    double Q[4];            // state penalty, row-major order
    double Pf[4];           // final prediction penalty, row-major order
    double R;               // control action penalty
    double state_bounds[2]; // |e1| <= state_bounds[0], |e2| <= state_bounds[1]
    double input_bound;     // |u| <= input_bound
    settings qp_options;    // qp solver options
    bool debug;             // print matrices etc
} MpcParams;

extern int trajectory_mpc(double e1, double e2, MpcParams mpc_params, double *u);
void y_Ax(double *A, double *x, double *y, unsigned int A_rows, unsigned int A_cols);
void kron_eye(double *M, size_t M_rows, size_t M_cols, size_t n, double *res);
void horzcat(double *M1, size_t M1_cols, double *M2, size_t M2_cols, size_t rows, double *res);
void blkdiag(double *M1, size_t M1_rows, size_t M1_cols, double *M2, size_t M2_rows, size_t M2_cols, double *res);
void messy_matrix_function(double *res, size_t res_rows, size_t res_cols, double *M, size_t M_rows, size_t M_cols);
void print_matrix(double *M, size_t rows, size_t cols);

int main(void)
{
    // Configure here
    MpcParams mpc_params = {{1.0, 2.0, 3.0, 4.0},
                            {1.0, 2.0},
                            3,
                            {1.0, 0.0, 0.0, 1.0},
                            {1.0, 0.0, 0.0, 1.0},
                            1.0,
                            {10.0, 20.0},
                            100.0,
                            {.verbose = 2,
                             .maxit = 30},
                            true};
    double x0[] = {0.0, 0.0};

    double u = 0.0;
    int exit_code = trajectory_mpc(x0[0], x0[1], mpc_params, &u);
    printf("u = %.4f\n", u);
    switch (exit_code)
    {
    case 0:
        printf("qp: optimal solution found\n");
        break;
    case 1:
        printf("qp: failure in solving LDL' factorization\n");
        break;
    case 2:
        printf("qp: maximum number of iterations exceeded\n");
        break;
    case 3:
        printf("qp: unknown problem in solver\n");
        break;
    default:
        printf("qp: unknown exit code: %i\n", exit_code);
        break;
    }
    return exit_code;
}

/**
 * @name Trajectory MPC
 *
 * @param[in] e1 TODO
 * @param[in] e2 TODO
 * @param[in] mpc_params TODO
 *
 * @param[out] u Input pointer
 * @result qp solver exit code
 *
 * @author Lucas Haglund
 */
extern int trajectory_mpc(double e1, double e2, MpcParams mpc_params, double *u)
{
    size_t N = mpc_params.N;

    // Inequality constraints
    // TODO: move this offline (i.e. not in the controller loop function)
    // F = [kron(eye(N), F_0); zeros(2 * N, 2 * N)]
    double F_0[] = {1.0, 0.0, -1.0, 0.0, 0.0, 1.0, 0.0, -1.0};
    size_t F_0_rows = 4;
    size_t F_0_cols = 2;
    size_t size_F_1 = F_0_rows * F_0_cols * N * N; // F_1: [4 * N, 2 * N]
    double *F_1 = malloc(size_F_1 * sizeof(double));
    kron_eye(F_0, F_0_rows, F_0_cols, N, F_1);
    size_t size_F = size_F_1 + 2 * 2 * N * N; // F: [6 * N, 2 * N]
    double *F = malloc(size_F * sizeof(double));
    memcpy(F, F_1, size_F_1 * sizeof(double));
    free(F_1);

    // G = [zeros(4 * N, N); kron(eye(N), G_0)]
    double G_0[] = {1.0, -1.0};
    size_t G_0_rows = 2;
    size_t G_0_cols = 1;
    size_t size_G_1 = G_0_rows * G_0_cols * N * N; // G_1: [2 * N, N]
    double *G_1 = malloc(size_G_1 * sizeof(double));
    kron_eye(G_0, G_0_rows, G_0_cols, N, G_1);
    size_t size_G = size_G_1 + 4 * N * N; // G: [6 * N, N]
    double *G = malloc(size_G * sizeof(double));
    memcpy(G + 4 * N * N, G_1, size_G_1 * sizeof(double));
    free(G_1);

    // Ain = [F, G]
    size_t size_Ain = size_F + size_G; // Ain: [6 * N, 3 * N]
    double *Ain = malloc(size_Ain * sizeof(double));
    horzcat(F, 2 * N, G, N, 6 * N, Ain);
    free(F);
    free(G);

    // bin = [kron(ones(N, 1), mpc_params.state_bounds); ones(2*n, 1) * mpc_params.input_bound]
    double h_0[] = {mpc_params.state_bounds[0],
                    mpc_params.state_bounds[0],
                    mpc_params.state_bounds[1],
                    mpc_params.state_bounds[1]};
    size_t size_h_1 = 4 * N; // h_1: [4 * N, 1]
    double *h_1 = malloc(size_h_1 * sizeof(double));
    for (int i = 0; i < N; i++)
    {
        memcpy(h_1 + i * 4, h_0, 4 * sizeof(double));
    }
    size_t size_h_2 = 2 * N; // h_2: [2 * N, 1]
    double *h_2 = malloc(size_h_2 * sizeof(double));
    for (int i = 0; i < size_h_2; i++)
    {
        h_2[i] = mpc_params.input_bound;
    }
    size_t size_bin = (size_h_1 + size_h_2) * 1; // bin: [6 * N, 1]
    double *bin = malloc(size_bin * sizeof(double));
    memcpy(bin, h_1, size_h_1 * sizeof(double));
    free(h_1);
    memcpy(bin + 4 * N, h_2, size_h_2 * sizeof(double));
    free(h_2);

    // Equality constraints
    // Q_bar = blkdiag(kron(eye(N - 1), Q), Pf)
    size_t Q_rows, Q_cols;
    Q_rows = Q_cols = 2;
    size_t size_Q_bar_0 = Q_rows * Q_cols * (N - 1) * (N - 1); // Q_bar_0: [2 * (N - 1), 2 * (N - 1)]
    double *Q_bar_0 = malloc(size_Q_bar_0 * sizeof(double));
    kron_eye(mpc_params.Q, Q_rows, Q_cols, N - 1, Q_bar_0);
    size_t size_Q_bar = (2 * (N - 1) + 2) * (2 * (N - 1) + 2); // Q_bar: [2 * N, 2 * N]
    double *Q_bar = malloc(size_Q_bar * sizeof(double));
    blkdiag(Q_bar_0, 2 * (N - 1), 2 * (N - 1), mpc_params.Pf, 2, 2, Q_bar);
    free(Q_bar_0);

    // R_bar = kron(eye(N), R)
    size_t size_R_bar = N * N; // R_bar: [N, N]
    double *R_bar = malloc(size_R_bar * sizeof(double));
    kron_eye(&mpc_params.R, 1, 1, N, R_bar);

    // H=blkdiag(Q_bar,R_bar);
    size_t size_H = (2 * N + N) * (2 * N + N); // H: [3 * N, 3 * N]
    double *H = malloc(size_H * sizeof(double));
    blkdiag(Q_bar, 2 * N, 2 * N, R_bar, N, N, H);
    free(Q_bar);
    free(R_bar);

    // f = []
    size_t H_rows = 3 * N;
    double *f = malloc(H_rows * sizeof(double));

    // Aeq1 = kron(eye(A_cols), eye(N))
    size_t A_rows, A_cols, B_rows;
    A_rows = A_cols = B_rows = 2;
    size_t size_Aeq_0 = N * N; // Aeq_0: [N, N]
    double *Aeq_0 = malloc(size_Aeq_0 * sizeof(double));
    double one = 1.0;
    kron_eye(&one, 1, 1, N, Aeq_0);                   // eye(N)
    size_t size_Aeq_1 = A_cols * A_cols * size_Aeq_0; // Aeq_1: [2 * N, 2 * N]
    double *Aeq_1 = malloc(size_Aeq_1 * sizeof(double));
    kron_eye(Aeq_0, N, N, A_cols, Aeq_1);
    free(Aeq_0);

    // [some messy lines from the matlab implementation]
    size_t Aeq_1_rows, Aeq_1_cols;
    Aeq_1_rows = Aeq_1_cols = 2 * N;
    messy_matrix_function(Aeq_1, Aeq_1_rows, Aeq_1_cols, mpc_params.A, A_rows, A_cols);

    // Aeq2 = kron(eye(N), -B)
    double B_neg[] = {-mpc_params.B[0], -mpc_params.B[1]};
    size_t size_Aeq_2 = 2 * N * 1 * N; // Aeq_2: [2 * N, N]
    double *Aeq_2 = malloc(size_Aeq_2 * sizeof(double));
    kron_eye(B_neg, 2, 1, N, Aeq_2);

    // Aeq = [Aeq1, Aeq2]
    size_t size_Aeq = size_Aeq_1 + size_Aeq_2;
    double *Aeq = malloc(size_Aeq * sizeof(double)); // Aeq: [2 * N, 3 * N]
    horzcat(Aeq_1, 2 * N, Aeq_2, N, 2 * N, Aeq);
    free(Aeq_1);
    free(Aeq_2);

    // beq = [A * x; zeros(A_cols * (N - 1), 1)]
    double x[] = {e1, e2};
    size_t size_Ax = 2; // Ax: [2, 1]
    double *Ax = malloc(size_Ax * sizeof(double));
    y_Ax(mpc_params.A, x, Ax, 2, 2);
    size_t size_beq = (2 + A_cols * (N - 1)) * 1; // beq: [2 * N, 1]
    double *beq = malloc(size_beq * sizeof(double));
    memcpy(beq, Ax, size_Ax);
    free(Ax);

    size_t size_in = size_bin;
    size_t size_eq = size_beq;
    QP *mpc_qp;

    // TODO: H = 2 .* H or not?
    for (int i = 0; i < size_H; i++)
    {
        H[i] *= 2.0;
    }

    // param order checked. wrong order in qpSWIFT Documentation.pdf
    mpc_qp = QP_SETUP_dense(H_rows,
                            size_in,
                            size_eq,
                            H,
                            Aeq,
                            Ain,
                            f,
                            bin,
                            beq,
                            NULL,
                            ROW_MAJOR_ORDERING);

    if (&mpc_params.qp_options != NULL)
    {
        *mpc_qp->options = mpc_params.qp_options;
    }

    qp_int qp_exit_code = QP_SOLVE(mpc_qp);
    *u = mpc_qp->x[2 * N];

    if (mpc_params.debug)
    {
        printf("DEBUG:\n\n");
        if (N <= 10)
        {
            typedef struct
            {
                char label[4];
                char dim[15];
                double *matrix;
                size_t rows;
                size_t cols;
            } MatrixDebug;
            MatrixDebug matrices[] = {{"H", "[3 * N, 3 * N]", H, H_rows, H_rows},
                                      {"f", "[3 * N, 1]", f, H_rows, 1},
                                      {"Ain", "[6 * N, 3 * N]", Ain, 6 * N, 3 * N},
                                      {"bin", "[6 * N, 1]", bin, 6 * N, 1},
                                      {"Aeq", "[2 * N, 3 * N]", Aeq, 2 * N, 3 * N},
                                      {"beq", "[2 * N, 1]", beq, 2 * N, 1}};
            printf("N = %li\n\n", N);
            for (int i = 0; i < 6; i++)
            {
                printf("%s: %s =\n", matrices[i].label, matrices[i].dim);
                print_matrix(matrices[i].matrix, matrices[i].rows, matrices[i].cols);
                printf("\n");
            }
        }
        else
        {
            printf("N = %li. Lower to <10 to get printable matrices.\n", N);
        }
        printf("\n");
    }

    QP_CLEANUP_dense(mpc_qp);
    free(H);
    free(Ain);
    free(Aeq);
    free(bin);
    free(beq);

    return (int)qp_exit_code;
}

void y_Ax(double *A, double *x, double *y, unsigned int A_rows, unsigned int A_cols)
{
    for (int i = 0; i < A_rows; i++)
    {
        y[i] = 0.0;
        for (int j = 0; j < A_cols; j++)
        {
            y[i] += A[i * A_cols + j] * x[j];
        }
    }
}

void kron_eye(double *M, size_t M_rows, size_t M_cols, size_t n, double *res)
{
    // In matlab syntax ish:
    // kron(eye(n), M) == kron_eye(M, size(M, 1), size(M, 2), n, res)

    for (int i = 0; i < M_rows * n; i++)
    {
        for (int j = (i / M_rows) * M_cols; j < (1 + i / M_rows) * M_cols; j++)
        {
            res[i * n * M_cols + j] = M[(i % M_rows) * M_cols + j % M_cols];
        }
    }
}

void test_kron_eye()
{
    double M[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    size_t M_rows = 2;
    size_t M_cols = 3;
    size_t n = 3;
    double *B = malloc(M_rows * n * M_cols * n * sizeof(double));
    kron_eye(M, M_rows, M_cols, n, B);
    print_matrix(B, M_rows * n, M_cols * n);
}

void horzcat(double *M1, size_t M1_cols, double *M2, size_t M2_cols, size_t rows, double *res)
{
    for (int i = 0; i < rows; i++)
    {
        memcpy(res + i * (M1_cols + M2_cols), M1 + i * M1_cols, M1_cols * sizeof(double));
        memcpy(res + i * (M1_cols + M2_cols) + M1_cols, M2 + i * M2_cols, M2_cols * sizeof(double));
    }
}

void blkdiag(double *M1, size_t M1_rows, size_t M1_cols, double *M2, size_t M2_rows, size_t M2_cols, double *res)
{
    size_t res_cols = M1_cols + M2_cols;
    for (int i = 0; i < M1_rows; i++)
    {
        memcpy(res + i * res_cols, M1 + i * M1_cols, M1_cols * sizeof(double));
    }
    for (int i = 0; i < M2_rows; i++)
    {
        memcpy(res + (M1_rows + i) * res_cols + M1_cols, M2 + i * M2_cols, M2_cols * sizeof(double));
    }
}

void test_blkdiag()
{
    double A[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    size_t A_rows = 3;
    size_t A_cols = 3;
    double B[] = {10.0, 20.0, 30.0, 40.0};
    size_t B_rows = 2;
    size_t B_cols = 2;
    size_t C_rows = A_rows + B_rows;
    size_t C_cols = A_cols + B_cols;
    size_t size_C = C_rows * C_cols;
    double *C = malloc(size_C * sizeof(double));
    blkdiag(A, A_rows, A_cols, B, B_rows, B_cols, C);
    print_matrix(C, C_rows, C_cols);
}

void messy_matrix_function(double *res, size_t res_rows, size_t res_cols, double *M, size_t M_rows, size_t M_cols)
{
    // Sort of copying their Aeq1 matlab implementation.
    // Will improve if I get the time.
    // Illustration of what it does:
    /*
    Aeq_1 = [ 1, 0, 0, 0, 0, 0,
              0, 1, 0, 0, 0, 0,
              0, 0, 1, 0, 0, 0,
              0, 0, 0, 1, 0, 0,
              0, 0, 0, 0, 1, 0,
              0, 0, 0, 0, 0, 1  ]
    A = [5, 6,
         7, 8 ]
    ->
    res = [ 1, 0, 0, 0, 0, 0,
            0, 1, 0, 0, 0, 0,
            5, 6, 1, 0, 0, 0,
            7, 8, 0, 1, 0, 0,
            0, 0, 5, 6, 1, 0,
            0, 0, 7, 8, 0, 1  ]
    */

    size_t size_indices = (res_cols - M_cols) / M_cols;
    uint32_t col_indices[size_indices];
    uint32_t *p_col_indices = (uint32_t *)&col_indices;
    for (uint32_t i = 0; i < res_cols - M_cols; i += M_cols)
    {
        *p_col_indices++ = i;
    }
    uint32_t row_indices[size_indices];
    uint32_t *p_row_indices = (uint32_t *)&row_indices;
    for (uint32_t i = M_rows; i < res_rows - M_rows; i += M_rows)
    {
        *p_row_indices++ = i;
    }
    for (int i = 0; i < size_indices; i++)
    {
        for (int j = 0; j < M_rows * M_cols; j++)
        {
            *(res + res_cols * row_indices[i] + col_indices[i] + (j / M_cols) * res_cols + (j % M_cols)) = M[j % (M_rows * M_cols)];
        }
    }
}

void print_matrix(double *M, size_t rows, size_t cols)
{
    printf("[ ");
    for (int i = 0; i < rows * cols; i++)
    {
        if (i != 0 && i % (cols) == 0)
        {
            printf("\n  ");
        }
        printf("%4.1f", M[i]);
        if (i != rows * cols - 1)
        {
            printf(", ");
        }
    }
    printf("  ]\n");
}

// not used
/*
typedef struct
{
    double g;      // gravity [m/s^2]
    double h;      // height of center of mass [m]
    double b;      // distance between wheel centers [m]
    double a;      // distance from rear wheel to frame's center of mass [m]
    double lambda; // fork axis angle [rad]
    double c;      // distance between front wheel contact point and the extention of the fork axis [m]
    double m;      // bike mass [kg]
} BikeParams;
*/
