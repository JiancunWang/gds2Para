//#include "stdafx.h"
#include <ctime>
#include "fdtd.hpp"
#include "hypreSolver.h"


static bool comp(pair<double, int> a, pair<double, int> b)
{
    return a.first <= b.first;
};

int setHYPREMatrix(myint *ARowId, myint *AColId, double *Aval, myint leng_v0, HYPRE_IJMatrix &a, HYPRE_ParCSRMatrix &parcsr_a){
    HYPRE_Int i;
    int myid, num_procs;
    HYPRE_Int N, n;

    HYPRE_Int ilower, iupper;
    HYPRE_Int local_size, extra;

    int solver_id;
    HYPRE_Int vis, print_system;

    double h, h2;

    HYPRE_IJMatrix A;
    HYPRE_ParCSRMatrix parcsr_A;

    /* Initialize MPI */
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    N = leng_v0;
    local_size = N / num_procs;
    extra = N - local_size * num_procs;

    ilower = local_size * myid;
    ilower += hypre_min(myid, extra);

    iupper = local_size * (myid + 1);
    iupper += hypre_min(myid + 1, extra);
    iupper = iupper - 1;

    /* How many rows do I have? */
    local_size = iupper - ilower + 1;

    /* Create the matrix.
    Note that this is a square matrix, so we indicate the row partition
    size twice (since number of rows = number of cols) */
    HYPRE_IJMatrixCreate(MPI_COMM_WORLD, ilower, iupper, ilower, iupper, &A);

    /* Choose a parallel csr format storage (see the User's Manual) */
    HYPRE_IJMatrixSetObjectType(A, HYPRE_PARCSR);

    /* Initialize before setting coefficients */
    HYPRE_IJMatrixInitialize(A);

    {
        HYPRE_Int nnz;
        vector<double> values;
        vector<HYPRE_Int> cols;
        int index = 0;

        for (i = ilower; i <= iupper; i++)
        {
            nnz = 0;   // Number of non-zeros on row i

            while (ARowId[index] == i){
                cols.push_back(AColId[index]);
                values.push_back(Aval[index]);
                //cout << ARowId[index] << " " << cols[nnz] << " " << values[nnz] << endl;
                nnz++;
                index++;
            }

            /* Set the values for row i */
            HYPRE_IJMatrixSetValues(A, 1, &nnz, &i, &cols[0], &values[0]);

            cols.clear();
            values.clear();
        }
    }

    /* Assemble after setting the coefficients */
    HYPRE_IJMatrixAssemble(A);

    /* Get the parcsr matrix object to use */
    HYPRE_IJMatrixGetObject(A, (void**)&parcsr_A);

    a = A;
    parcsr_a = parcsr_A;

    return 0;
}

int paraGenerator(fdtdMesh *sys, unordered_map<double, int> xi, unordered_map<double, int> yi, unordered_map<double, int> zi){
    int bdn;   // # of boundary
#ifdef UPPER_BOUNDARY_PEC
    bdn = 2;
#else
    bdn = 1;
#endif

    myint indi, j, mark, k, l, n;
    int status;
    int count;
    int xcol;
    vector<int> rowId;
    vector<int> colId;
    vector<double> val;
    vector<int> temp2;
    int inx, iny, inz;

    /* Construct V0d with row id, col id and its val */
    myint leng_v0d1 = 0, v0d1num = 0;    // store the num of v0d1 vectors, which are nodes outside the conductors
    myint leng_v0d1a = 0, v0d1anum = 0;
    myint leng_Ad = 0;
    myint *map = (myint*)calloc(sys->N_node, (sizeof(myint)));
    double block1_x, block1_y, block2_x, block2_y, block3_x, block3_y, sideLen;
    //unordered_map<myint, double> Ad1;
    unordered_map<myint, unordered_map<myint, double>> Ad1;

    block1_x = 0;// (sys->xlim2 - sys->xlim1) / 5 * sys->lengthUnit;
    block1_y = 0;// (sys->ylim2 - sys->ylim1) / 5 * sys->lengthUnit;
    block2_x = 0;// (sys->xlim2 - sys->xlim1) / 5 * sys->lengthUnit;
    block2_y = 0;// (sys->ylim2 - sys->ylim1) / 5 * sys->lengthUnit;
    block3_x = 0;
    block3_y = 0;
#ifdef PRINT_V0D_BLOCKS
    cout << "V0d's block1_x and block1_y are " << block1_x << " " << block1_y << endl;
    cout << "V0d's block2_x and block2_y are " << block2_x << " " << block2_y << endl;
    cout << "V0d's block3_x and block3_y are " << block3_x << " " << block3_y << endl;
#endif
    sideLen = 0;    // around the conductor 10um is considered with rapid potential change


    clock_t t1 = clock();
    clock_t ts = t1;
    status = merge_v0d1(sys, block1_x, block1_y, block2_x, block2_y, block3_x, block3_y, v0d1num, leng_v0d1, v0d1anum, leng_v0d1a, map, sideLen);
    //cout << "Merge V0d1 time is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
    myint node1, node2;

    t1 = clock();
    cout << "Length of V0d1 is " << leng_v0d1 << ", and number of non-zeros in V0d1 is " << v0d1num << endl;
    cout << "Length of V0d1a is " << leng_v0d1a << ", and number of non-zeros in V0d1a is " << v0d1anum << endl;
    cout << "V0d is generated!" << endl;

    for (indi = 0; indi < v0d1anum; indi++){    // the lower planes are PEC
        if (sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v) >= sys->N_edge_s){    // this edge is along z axis
            inz = sys->v0d1RowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = ((sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - sys->N_edge_s) / (sys->N_cell_y + 1);
            iny = ((sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - sys->N_edge_s) % (sys->N_cell_y + 1);
            node1 = inz * sys->N_node_s + (sys->N_cell_y + 1) * inx + iny;
            node2 = (inz + 1) * sys->N_node_s + (sys->N_cell_y + 1) * inx + iny;
            if (map[node1] != sys->v0d1ColId[indi] + 1 && map[node1] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node1] - 1] += sys->v0d1aval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * (-1) / (sys->zn[inz + 1] - sys->zn[inz]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else if (map[node2] != sys->v0d1ColId[indi] + 1 && map[node2] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node2] - 1] += sys->v0d1aval[indi] * (-1) / (sys->zn[inz + 1] - sys->zn[inz]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else {//if (map[sys->edgelink[sys->v0d1aRowId[i] * 2]] == 0 || map[sys->edgelink[sys->v0d1aRowId[i] * 2] + 1] == 0){
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += abs(sys->v0d1aval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0);
            }
        }
        else if (sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v) >= (sys->N_cell_y) * (sys->N_cell_x + 1)){    // this edge is along x axis
            inz = sys->v0d1RowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = ((sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) / (sys->N_cell_y + 1);
            iny = ((sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) % (sys->N_cell_y + 1);
            node1 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny;
            node2 = inz * sys->N_node_s + (inx + 1) * (sys->N_cell_y + 1) + iny;
            if (map[node1] != sys->v0d1ColId[indi] + 1 && map[node1] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node1] - 1] += sys->v0d1aval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * (-1) / (sys->xn[inx + 1] - sys->xn[inx]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else if (map[node2] != sys->v0d1ColId[indi] + 1 && map[node2] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node2] - 1] += sys->v0d1aval[indi] * (-1) / (sys->xn[inx + 1] - sys->xn[inx]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else {//if (map[sys->edgelink[sys->v0d1aRowId[i] * 2]] == 0 || map[sys->edgelink[sys->v0d1aRowId[i] * 2] + 1] == 0){
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += abs(sys->v0d1aval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0);
            }
        }
        else{    // this edge is along y axis
            inz = sys->v0d1RowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = (sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) / sys->N_cell_y;
            iny = (sys->v0d1RowId[indi] % (sys->N_edge_s + sys->N_edge_v)) % sys->N_cell_y;
            node1 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny;
            node2 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny + 1;
            if (map[node1] != sys->v0d1ColId[indi] + 1 && map[node1] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node1] - 1] += sys->v0d1aval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * (-1) / (sys->yn[iny + 1] - sys->yn[iny]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else if (map[node2] != sys->v0d1ColId[indi] + 1 && map[node2] != 0){
                Ad1[sys->v0d1ColId[indi]][map[node2] - 1] += sys->v0d1aval[indi] * (-1) / (sys->yn[iny + 1] - sys->yn[iny]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += sys->v0d1aval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
            }
            else {//if (map[sys->edgelink[sys->v0d1aRowId[i] * 2]] == 0 || map[sys->edgelink[sys->v0d1aRowId[i] * 2] + 1] == 0){
                Ad1[sys->v0d1ColId[indi]][sys->v0d1ColId[indi]] += abs(sys->v0d1aval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0);
            }
        }
    }


    for (indi = 0; indi < leng_v0d1; indi++){
        leng_Ad += Ad1[indi].size();
    }


    sys->v0d1valo = (double*)malloc(v0d1num * sizeof(double));
    for (indi = 0; indi < v0d1num; indi++){
        sys->v0d1valo[indi] = sys->v0d1val[indi];
    }
    for (indi = 0; indi < v0d1num; indi++){    // compute sqrt(D_eps)*V0d1
        sys->v0d1val[indi] = sys->v0d1val[indi] * sqrt(sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0);
    }
    sys->v0d1avalo = (double*)malloc(v0d1anum * sizeof(double));
    for (indi = 0; indi < v0d1anum; indi++) {
        sys->v0d1avalo[indi] = sys->v0d1aval[indi];
    }
    for (indi = 0; indi < v0d1anum; indi++){
        sys->v0d1aval[indi] = sys->v0d1aval[indi] * sqrt(sys->stackEpsn[(sys->v0d1RowId[indi] + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0);
    }


    myint leng_v0d = leng_v0d1;
    sys->v0d1ColIdo = (myint*)malloc(v0d1num * sizeof(myint));
    for (indi = 0; indi < v0d1num; indi++)
        sys->v0d1ColIdo[indi] = sys->v0d1ColId[indi];
    free(sys->v0d1ColId); sys->v0d1ColId = (myint*)malloc((leng_v0d1 + 1) * sizeof(myint));
    status = COO2CSR_malloc(sys->v0d1ColIdo, sys->v0d1RowId, sys->v0d1val, v0d1num, leng_v0d1, sys->v0d1ColId);
    if (status != 0)
        return status;
    free(sys->v0d1val); sys->v0d1val = NULL;
    free(sys->v0d1ColIdo); sys->v0d1ColIdo = NULL;

    /*sys->v0d1aColIdo = (myint*)malloc(v0d1anum * sizeof(myint));
    for (indi = 0; indi < v0d1anum; indi++)
        sys->v0d1aColIdo[indi] = sys->v0d1aColId[indi];
    free(sys->v0d1aColId); sys->v0d1aColId = (myint*)malloc((leng_v0d1a + 1) * sizeof(myint));
    status = COO2CSR_malloc(sys->v0d1aColIdo, sys->v0d1aRowId, sys->v0d1aval, v0d1anum, leng_v0d1a, sys->v0d1aColId);
    if (status != 0)
        return status;*/
    free(sys->v0d1aval); sys->v0d1aval = NULL;
    

    
    //cout << "Number of NNZ in V0d1 is " << v0d1num << endl;

    sparse_status_t s;

    /* V0d^T's csr form handle for MKL */
    sparse_matrix_t V0dt;
    s = mkl_sparse_d_create_csr(&V0dt, SPARSE_INDEX_BASE_ZERO, leng_v0d1, sys->N_edge, &sys->v0d1ColId[0], &sys->v0d1ColId[1], sys->v0d1RowId, sys->v0d1valo);

    /* V0da^T's csr form handle for MKL */
    sparse_matrix_t V0dat;
    s = mkl_sparse_d_create_csr(&V0dat, SPARSE_INDEX_BASE_ZERO, leng_v0d1, sys->N_edge, &sys->v0d1ColId[0], &sys->v0d1ColId[1], sys->v0d1RowId, sys->v0d1avalo);

    

    ///******************************************************************/
    ///* use MKL to do matrix multiplication */
    ///* Note that the result for each row, the col # is not in the increased order */
    /*leng_Ad = 0;
    clock_t t2 = clock();
    status = mklMatrixMulti(sys, leng_Ad, sys->v0d1aRowId, sys->v0d1aColId, sys->v0d1aval, sys->N_edge, leng_v0d1, sys->v0d1RowId, sys->v0d1ColId, sys->v0d1val, 1);
    cout << "Matrix mutiplication time is " << (clock() - t2) * 1.0 / CLOCKS_PER_SEC << " s" << endl;*/

    ///*****************************************************************/
    sys->AdRowId = (myint*)calloc(leng_Ad, sizeof(myint));
    sys->AdColId = (myint*)calloc(leng_Ad, sizeof(myint));
    sys->Adval = (double*)calloc(leng_Ad, sizeof(double));
    j = 0;
    
    /*for (indi = 0; indi < leng_Ad; indi++){
        sys->AdRowId[j] = AdRowId[indi];
        sys->AdColId[j] = AdColId[indi];
        sys->Adval[j] = Adval[indi];
        j++;
    }
    free(AdRowId); AdRowId = NULL;
    free(AdColId); AdColId = NULL;
    free(Adval); Adval = NULL;*/
    for (indi = 0; indi < leng_v0d1; indi++){
        vector<pair<myint, double>> v(Ad1[indi].begin(), Ad1[indi].end());
        sort(v.begin(), v.end());
        for (auto adi : v){
            if (abs(adi.second) > 1e-8){
                sys->AdRowId[j] = indi;
                sys->AdColId[j] = adi.first;
                sys->Adval[j] = adi.second;
                j++;
            }
        }
        v.clear();
    }
    Ad1.clear();
    //cout << "Time to generate Ad is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl;

    //cout << "Number of non-zeros in Ad is " << leng_Ad << endl;
    int *argc;
    char ***argv;
    /*  trial of first set HYPRE matrix Ad */
    //HYPRE_IJMatrix ad;
    //HYPRE_ParCSRMatrix parcsr_ad;
    MPI_Init(argc, argv);
    //status = setHYPREMatrix(sys->AdRowId, sys->AdColId, sys->Adval, leng_v0d1, ad, parcsr_ad);
    /* End */

    //sys->AdRowId1 = (int*)malloc((leng_v0d1 + 1) * sizeof(int));
    //status = COO2CSR_malloc(sys->AdRowId, sys->AdColId, sys->Adval, leng_Ad, leng_v0d1, sys->AdRowId1);
    //if (status != 0)
    //    return status;

    //cout << "The number of nonzeros in Ad is " << leng_Ad << endl;

    /* Construct V0c with row id, col id and its val */
    myint leng_v0c = 0, v0cnum = 0;
    myint leng_v0ca = 0, v0canum = 0;

    int numPortCdt = 0;
    myint leng_Ac = 0;
    count = 0;



    sys->cindex.push_back(-1);    // the last index in the sparse form for each conductor in V0c, i denotes ith conductor (starting from 1)
    sys->acu_cnno.push_back(0);    // how many v0c are in each conductor (accumulative), i denotes the ith conductor (starting from 1)

    unordered_map<myint, unordered_map<myint, double>> Ac;
    count = 0;
    free(map);
    map = (myint*)calloc(sys->N_node, sizeof(myint));
    block1_x = 0;// (sys->xlim2 - sys->xlim1) / 3 * sys->lengthUnit;// (sys->xn[sys->nx - 1] - sys->xn[0]) / 10;
    block1_y = 0;// (sys->ylim2 - sys->ylim1) / 3 * sys->lengthUnit;// (sys->yn[sys->ny - 1] - sys->yn[0]) / 10;
    block2_x = 0;// (sys->xlim2 - sys->xlim1) / 5 * sys->lengthUnit;
    block2_y = 0;// (sys->ylim2 - sys->ylim1) / 5 * sys->lengthUnit;
#ifdef PRINT_V0C_BLOCKS
    cout << "V0c's block1_x and block1_y are " << block1_x << " " << block1_y << endl;
    cout << "V0c's block2_x and block2_y are " << block2_x << " " << block2_y << endl;
#endif
    
    t1 = clock();
    status = merge_v0c(sys, block1_x, block1_y, block2_x, block2_y, v0cnum, leng_v0c, v0canum, leng_v0ca, map);

    //cout << "Time to generate V0c is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl; 
    cout << "Length of V0c is " << leng_v0c << " number of non-zeros in V0c is " << v0cnum << endl;
    cout << "Length of V0ca is " << leng_v0ca << " number of non-zeros in V0ca is " << v0canum << endl;
    cout << "V0c is generated!" << endl;

    j = 0;

    /*for (i = 0; i < sys->numCdt + 1; i++){
    cout << sys->acu_cnno[i] << " ";
    }
    cout << endl;*/
    t1 = clock();
    for (indi = 0; indi < v0canum; indi++){    // the upper and lower planes are PEC
        if (sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v) >= sys->N_edge_s){    // this edge is along z axis
            inz = sys->v0cRowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = ((sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - sys->N_edge_s) / (sys->N_cell_y + 1);
            iny = ((sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - sys->N_edge_s) % (sys->N_cell_y + 1);
            node1 = inz * sys->N_node_s + (sys->N_cell_y + 1) * inx + iny;
            node2 = (inz + 1) * sys->N_node_s + (sys->N_cell_y + 1) * inx + iny;
            if (map[node1] != sys->v0cColId[indi] + 1 && map[node1] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node1] - 1] += sys->v0caval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * (-1) / (sys->zn[inz + 1] - sys->zn[inz]) * SIGMA;
            }
            else if (map[node2] != sys->v0cColId[indi] + 1 && map[node2] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node2] - 1] += sys->v0caval[indi] * (-1) / (sys->zn[inz + 1] - sys->zn[inz]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * SIGMA;
            }
            else if (sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += abs(sys->v0caval[indi] * 1 / (sys->zn[inz + 1] - sys->zn[inz]) * SIGMA);
            }
        }
        else if (sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v) >= (sys->N_cell_y) * (sys->N_cell_x + 1)){    // this edge is along x axis
            inz = sys->v0cRowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = ((sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) / (sys->N_cell_y + 1);
            iny = ((sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) % (sys->N_cell_y + 1);
            node1 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny;
            node2 = inz * sys->N_node_s + (inx + 1) * (sys->N_cell_y + 1) + iny;
            if (map[node1] != sys->v0cColId[indi] + 1 && map[node1] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node1] - 1] += sys->v0caval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * (-1) / (sys->xn[inx + 1] - sys->xn[inx]) * SIGMA;
            }
            else if (map[node2] != sys->v0cColId[indi] + 1 && map[node2] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node2] - 1] += sys->v0caval[indi] * (-1) / (sys->xn[inx + 1] - sys->xn[inx]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * SIGMA;
            }
            else if (sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += abs(sys->v0caval[indi] * 1 / (sys->xn[inx + 1] - sys->xn[inx]) * SIGMA);
            }
        }
        else{    // this edge is along y axis
            inz = sys->v0cRowId[indi] / (sys->N_edge_s + sys->N_edge_v);
            inx = (sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) / sys->N_cell_y;
            iny = (sys->v0cRowId[indi] % (sys->N_edge_s + sys->N_edge_v)) % sys->N_cell_y;
            node1 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny;
            node2 = inz * sys->N_node_s + inx * (sys->N_cell_y + 1) + iny + 1;
            if (map[node1] != sys->v0cColId[indi] + 1 && map[node1] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node1] - 1] += sys->v0caval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * (-1) / (sys->yn[iny + 1] - sys->yn[iny]) * SIGMA;
            }
            else if (map[node2] != sys->v0cColId[indi] + 1 && map[node2] != 0 && sys->markEdge[sys->v0cRowId[indi]] != 0){
                Ac[sys->v0cColId[indi]][map[node2] - 1] += sys->v0caval[indi] * (-1) / (sys->yn[iny + 1] - sys->yn[iny]) * SIGMA;
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += sys->v0caval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * SIGMA;
            }
            else if (sys->markEdge[sys->v0cRowId[indi]] != 0) {
                Ac[sys->v0cColId[indi]][sys->v0cColId[indi]] += abs(sys->v0caval[indi] * 1 / (sys->yn[iny + 1] - sys->yn[iny]) * SIGMA);
            }
        }

        
    }

    for (indi = 0; indi < leng_v0c; indi++){
        leng_Ac += Ac[indi].size();
    }
    sys->AcRowId = (myint*)calloc(leng_Ac, sizeof(myint));
    sys->AcColId = (myint*)calloc(leng_Ac, sizeof(myint));
    sys->Acval = (double*)calloc(leng_Ac, sizeof(double));
    j = 0;
    k = 1;
    myint collar = 0;
    for (indi = 0; indi < leng_v0c; indi++){
        vector<pair<myint, double>> v(Ac[indi].begin(), Ac[indi].end());
        sort(v.begin(), v.end());
        for (auto aci : v){
            if (abs(aci.second) > 1e5){
                sys->AcRowId[j] = indi;
                sys->AcColId[j] = aci.first;
                sys->Acval[j] = aci.second;
                if (sys->AcRowId[j] >= sys->acu_cnno[k]){
                    sys->cindex.push_back(j - 1);
                    k++;
                }
                j++;
            }
        }
        v.clear();
    }
    sys->cindex.push_back(j - 1);
    //cout << "Time to generate Ac is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl;
    //cout << "Number of non-zeros in Ac is " << leng_Ac << endl;
    Ac.clear();
    free(sys->markNode); sys->markNode = NULL;
    
    for (indi = 0; indi < sys->numCdt; indi++){
        free(sys->conductor[indi].node); sys->conductor[indi].node = NULL;
    }
    free(sys->conductor); sys->conductor = NULL;
    /*  trial of first set HYPRE matrix Ac */
    //HYPRE_IJMatrix ac;
    //HYPRE_ParCSRMatrix parcsr_ac;
    //status = setHYPREMatrix(sys->AcRowId, sys->AcColId, sys->Acval, leng_v0c, ac, parcsr_ac);
    /* End */


    sys->v0cvalo = (double*)malloc(v0cnum * sizeof(double));
    for (indi = 0; indi < v0cnum; indi++)
        sys->v0cvalo[indi] = sys->v0cval[indi];    // v0cvalo is the v0c values without D_sig
    for (indi = 0; indi < v0cnum; indi++){
        if (sys->markEdge[sys->v0cRowId[indi]] != 0){
            sys->v0cval[indi] = sys->v0cval[indi] * sqrt(SIGMA);       // Compute the sparse form of D_sig*V0c
        }
    }
    sys->v0cavalo = (double*)malloc(v0canum * sizeof(double));
    for (indi = 0; indi < v0canum; indi++)
        sys->v0cavalo[indi] = sys->v0caval[indi];
    for (indi = 0; indi < v0canum; indi++){
        if (sys->markEdge[sys->v0cRowId[indi]] != 0){
            sys->v0caval[indi] = sys->v0caval[indi] * sqrt(SIGMA);
        }
    }
    


    sys->v0cColIdo = (myint*)malloc(v0cnum * sizeof(myint));
    for (indi = 0; indi < v0cnum; indi++)
        sys->v0cColIdo[indi] = sys->v0cColId[indi];
    free(sys->v0cColId); sys->v0cColId = (myint*)malloc((leng_v0c + 1) * sizeof(myint));
    status = COO2CSR_malloc(sys->v0cColIdo, sys->v0cRowId, sys->v0cval, v0cnum, leng_v0c, sys->v0cColId);
    if (status != 0)
        return status;
    free(sys->v0cColIdo); sys->v0cColIdo = NULL;
    free(sys->v0cval); sys->v0cval = NULL;

    /*sys->v0caColIdo = (myint*)malloc(v0canum * sizeof(myint));
    for (indi = 0; indi < v0canum; indi++)
        sys->v0caColIdo[indi] = sys->v0caColId[indi];
    free(sys->v0caColId); sys->v0caColId = (myint*)malloc((leng_v0ca + 1)*sizeof(myint));
    status = COO2CSR_malloc(sys->v0caColIdo, sys->v0caRowId, sys->v0caval, v0canum, leng_v0ca, sys->v0caColId);
    if (status != 0)
        return status;
    free(sys->v0caColIdo); sys->v0caColIdo = NULL;*/
    free(sys->v0caval); sys->v0caval = NULL;


    //status = mklMatrixMulti(sys, leng_Ac, sys->v0caRowId, sys->v0caColId, sys->v0caval, sys->N_edge, leng_v0c, sys->v0cRowId, sys->v0cColId, sys->v0cval, 2);

    //cout << "leng v0c " << leng_v0c  << " number of non-zeros in V0c is " << v0cnum << endl;
    //cout << "The number of nonzeros in Ac is " << leng_Ac << endl;

    /*for (i = 0; i < sys->numCdt + 1; i++){
    cout << sys->acu_cnno[i] << " ";
    }
    cout << endl;
    for (i = 0; i < sys->numCdt + 1; i++){
    cout << sys->cindex[i] << " ";
    }
    cout << endl;*/

    /* Compute the matrix V0c'*D_sig*V0c */
    /*status = matrixMulti(sys->v0caColId, sys->v0caRowId, sys->v0caval, sys->v0cRowId, sys->v0cColId, sys->v0cval, sys->AcRowId, sys->AcColId, sys->Acval);
    if (status != 0)
    return status;*/

    /* double *d = &(sys->Adval[0]);
    int *id = &(sys->AdRowId1[0]);
    int *jd = &(sys->AdColId[0]);*/

    double *a;
    int *ia, *ja;


    /* Pick up a y0c2 cooresponding to one source port */
    complex<double> Zresult;
    double *bd1, *bd2;
    double *bdc1, *bdc2;
    double *temp;
    double mdone;
    int ione;
    lapack_int *ipiv;
    int info;
    double *workspace;
    double *xd2;
    double *temp1;
    int startCol;
    startCol = 0;
    sys->Y = (complex<double>*)calloc(sys->numPorts * sys->numPorts * sys->nfreq, sizeof(complex<double>));
    sys->x = { };
    for (indi = 0; indi < sys->numPorts*sys->numPorts*sys->nfreq; indi++){
        sys->x.emplace_back(complex<double>(0., 0.)); // Complex double constructor from real and imaginary
    }


    double *b, *xc;    // the array of the right hand side
    xcol = 0;

    int port, sourcePort;    // show which port it is using
    int node;
    double *crhs;

    char transa;
    int m;
    double alpha = 1, beta = 0;
    char matdescra[6];
    matdescra[0] = 'G'; matdescra[3] = 'C';    // general matrix multi, 0-based indexing
    cout << endl << "Begin to solve for network parameters!" << endl;

    double *ydcp;
    double *y0c, *y0cs;
    double *yc, *yca;
    double *yccp;
    double *dRhs, *dRhs2, *crhss;
    complex<double> *yd;
    double *yd1, *yd2, *yd2a;
    double *v0caJ, *v0daJ, *y0d, *ydt, *y0d2, *ydat;
    double leng;
    lapack_complex_double *u0, *u0a;
    double *u0d, *u0c;
    double nn, nna;
#ifndef SKIP_VH
    complex<double>* xr;
    lapack_complex_double *Vh;
#endif

    sourcePort = 0;
    struct matrix_descr descr;


    /* V0ca^T's csr form handle for MKL */
    sparse_matrix_t V0cat;
    s = mkl_sparse_d_create_csr(&V0cat, SPARSE_INDEX_BASE_ZERO, leng_v0c, sys->N_edge, &sys->v0cColId[0], &sys->v0cColId[1], sys->v0cRowId, sys->v0cavalo);

    /* V0c^T's csr form handle for MKL */
    sparse_matrix_t V0ct;
    s = mkl_sparse_d_create_csr(&V0ct, SPARSE_INDEX_BASE_ZERO, leng_v0c, sys->N_edge, &sys->v0cColId[0], &sys->v0cColId[1], sys->v0cRowId, sys->v0cvalo);
    
    
    lapack_complex_double *tmp;
    lapack_complex_double *m_h, *m_hc;
    lapack_complex_double *rhs_h, *rhs_h0;
    lapack_complex_double *J;
    lapack_complex_double *y_h;
    lapack_int info1;
    lapack_int iter;
    lapack_complex_double *final_x;
    lapack_complex_double *J_h;
    lapack_complex_double *y_re, *tmp3, *tmp4, *y_new, *m_new;
    double *ferr, *berr;
    double freq;
    myint start, inde;
    

    /* HYPRE solves for each port are messy */
    while (sourcePort < sys->numPorts){
        t1 = clock();
        cout << "Source id is " << sourcePort << endl;
        sys->J = (double*)calloc(sys->N_edge, sizeof(double));
        for (indi = 0; indi < sys->portEdge[sourcePort].size(); indi++){
            sys->J[sys->portEdge[sourcePort][indi]] = sys->portCoor[sourcePort].portDirection;
        }

        
        v0daJ = (double*)calloc(leng_v0d1, sizeof(double));
        y0d = (double*)calloc(leng_v0d1, sizeof(double));

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
       
        s = mkl_sparse_d_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, V0dat, descr, sys->J, beta, v0daJ);
        for (indi = 0; indi < leng_v0d1; indi++){
            v0daJ[indi] = -v0daJ[indi];
        }
        
        /* solve V0d system */


        //status = hypreSolve(sys, ad, parcsr_ad, leng_Ad, v0daJ, leng_v0d1, y0d);
        status = hypreSolve(sys, sys->AdRowId, sys->AdColId, sys->Adval, leng_Ad, v0daJ, leng_v0d1, y0d);
        
        /* End of solving */

#ifndef SKIP_PARDISO
        t1 = clock();
        status = solveV0dSystem(sys, v0daJ, y0d, leng_v0d1);
        cout << " Pardiso solve time " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
#endif
        t1 = clock();
        for (indi = 0; indi < leng_v0d1; indi++){
            y0d[indi] = y0d[indi] / (2 * M_PI * sys->freqStart * sys->freqUnit);    // y0d is imaginary
        }
        ydt = (double*)calloc(sys->N_edge, sizeof(double));
        ydat = (double*)calloc(sys->N_edge, sizeof(double));
        yd1 = (double*)malloc(sys->N_edge * sizeof(double));

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0dt, descr, y0d, beta, ydt);    // -V0d*(D_eps0\(V0da'*rsc))
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0dat, descr, y0d, beta, ydat);    // -V0da*(D_eps0\(V0da'*rsc))

        u0 = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * 2, sizeof(lapack_complex_double));
        u0a = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * 2, sizeof(lapack_complex_double));
        nn = 0;
        nna = 0;
        for (indi = sys->N_edge_s; indi < sys->N_edge; indi++){
            nn += ydt[indi] * ydt[indi];
            nna += ydat[indi] * ydat[indi];
        }
        nn = sqrt(nn);
        nna = sqrt(nna);
        for (indi = sys->N_edge_s; indi < sys->N_edge; indi++){
            u0[indi - sys->N_edge_s].real = ydt[indi] / nn;    // u0d is one vector in V0
            u0a[indi - sys->N_edge_s].real = ydat[indi] / nna;    // u0da
        }

        /* Compute C right hand side */
        y0c = (double*)calloc(leng_v0c, sizeof(double));
        v0caJ = (double*)calloc(leng_v0c, sizeof(double));

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, V0cat, descr, sys->J, beta, v0caJ);
        // myint count_non = 0; // This is not used later in the function but was used for printing earlier
        for (indi = 0; indi < leng_v0c; indi++){
            v0caJ[indi] = -v0caJ[indi];
        }

        crhs = (double*)calloc(leng_v0c, sizeof(double));
        for (indi = 0; indi < sys->N_edge; indi++){
            yd1[indi] = ydt[indi];
            ydt[indi] = -ydt[indi] * (2 * M_PI*sys->freqStart * sys->freqUnit) * sys->stackEpsn[(indi + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
        }

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, V0cat, descr, ydt, beta, crhs);
        free(ydt); ydt = NULL;

        double v0caJn, crhsn;
        v0caJn = 0;
        crhsn = 0;
        for (indi = 0; indi < leng_v0c; indi++){
            v0caJ[indi] = (v0caJ[indi] + crhs[indi]);
            v0caJn += v0caJ[indi] * v0caJ[indi];
            crhsn += crhs[indi] * crhs[indi];
        }
        v0caJn = sqrt(v0caJn);
        crhsn = sqrt(crhsn);
        free(crhs); crhs = NULL;


        //cout << "Time between the first and the second HYPRE is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl;
        /*solve c system block by block*/
        t1 = clock();
        for (indi = 1; indi <= 1; indi++){
#ifndef SKIP_PARDISO
            //pardisoSolve_c(sys, &v0caJ[sys->acu_cnno[i - 1]], &y0c[sys->acu_cnno[i - 1]], sys->acu_cnno[i - 1], sys->acu_cnno[i] - 1, sys->cindex[i - 1] + 1, sys->cindex[i]);
#endif

            //a = (double*)malloc((sys->cindex[i] - sys->cindex[i - 1]) * sizeof(double));
            //ia = (int*)malloc((sys->cindex[i] - sys->cindex[i - 1]) * sizeof(int));
            //ja = (int*)malloc((sys->cindex[i] - sys->cindex[i - 1]) * sizeof(int));
            //crhss = (double*)malloc((sys->acu_cnno[i] - sys->acu_cnno[i - 1]) * sizeof(double));
            //y0cs = (double*)calloc((sys->acu_cnno[i] - sys->acu_cnno[i - 1]), sizeof(double));

            //for (j = sys->acu_cnno[i - 1]; j <= sys->acu_cnno[i] - 1; j++){
            //    crhss[j - sys->acu_cnno[i - 1]] = v0caJ[j];
            //}
            //for (j = sys->cindex[i - 1] + 1; j <= sys->cindex[i]; j++){
            //    a[j - sys->cindex[i - 1] - 1] = sys->Acval[j];
            //    ia[j - sys->cindex[i - 1] - 1] = sys->AcRowId[j] - sys->AcRowId[sys->cindex[i - 1] + 1];
            //    ja[j - sys->cindex[i - 1] - 1] = sys->AcColId[j] - sys->AcColId[sys->cindex[i - 1] + 1];
            //    //cout << a[j - sys->cindex[i - 1] - 1] << " " << ia[j - sys->cindex[i - 1] - 1] << " " << ja[j - sys->cindex[i - 1] - 1] << endl;
            //}

            //status = hypreSolve(sys, ia, ja, a, (sys->cindex[i] - sys->cindex[i - 1]), crhss, (sys->acu_cnno[i] - sys->acu_cnno[i - 1]), y0cs);

            //for (j = sys->acu_cnno[i - 1]; j <= sys->acu_cnno[i] - 1; j++){
            //    y0c[j] = y0cs[j - sys->acu_cnno[i - 1]];
            //}
            //if (sys->acu_cnno[i] - sys->acu_cnno[i - 1] > 100000){
            //    outfile1.open("Ac.txt", std::ofstream::out | std::ofstream::trunc);
            //    for (j = 0; j < sys->cindex[i] - sys->cindex[i - 1]; j++){
            //        outfile1 << sys->AcRowId[i] + 1 << " " << sys->AcColId[i] + 1 << " " << sys->Acval[i] << endl;
            //    }
            //    outfile1.close();
            //}
            //free(a); a = NULL;
            //free(ia); ia = NULL;
            //free(ja); ja = NULL;
            //free(crhss); crhss = NULL;
            //free(y0cs); y0cs = NULL;

            //status = hypreSolve(sys, ac, parcsr_ac, leng_Ac, v0caJ, leng_v0c, y0c);
            status = hypreSolve(sys, sys->AcRowId, sys->AcColId, sys->Acval, leng_Ac, v0caJ, leng_v0c, y0c);

        }

        free(v0caJ); v0caJ = NULL;
        //cout << "HYPRE solve time is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;


        t1 = clock();

        /* V0cy0c */
        yc = (double*)calloc(sys->N_edge, sizeof(double));
        yca = (double*)calloc(sys->N_edge, sizeof(double));
        yccp = (double*)malloc(sys->N_edge * sizeof(double));
        dRhs2 = (double*)calloc(leng_v0d1, sizeof(double));
        y0d2 = (double*)calloc(leng_v0d1, sizeof(double));

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0ct, descr, y0c, beta, yc);
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0cat, descr, y0c, beta, yca);

        free(y0c); y0c = NULL;

        for (indi = 0; indi < sys->N_edge; indi++){
            yccp[indi] = -yc[indi] * sys->stackEpsn[(indi + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
        }

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, V0dat, descr, yccp, beta, dRhs2);
        //cout << "Time between the second and the third HYPRE is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl;
        free(yccp); yccp = NULL;

        t1 = clock();
        //status = hypreSolve(sys, ad, parcsr_ad, leng_Ad, dRhs2, leng_v0d1, y0d2);
        status = hypreSolve(sys, sys->AdRowId, sys->AdColId, sys->Adval, leng_Ad, dRhs2, leng_v0d1, y0d2);

        free(dRhs2); dRhs2 = NULL;
        // cout << "HYPRE solve time is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;

        t1 = clock();
        yd2 = (double*)calloc(sys->N_edge, sizeof(double));
        yd2a = (double*)calloc(sys->N_edge, sizeof(double));

        alpha = 1;
        beta = 0;
        descr.type = SPARSE_MATRIX_TYPE_GENERAL;
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0dt, descr, y0d2, beta, yd2);
        s = mkl_sparse_d_mv(SPARSE_OPERATION_TRANSPOSE, alpha, V0dat, descr, y0d2, beta, yd2a);
        free(y0d2); y0d2 = NULL;
        nn = 0;
        nna = 0;
        for (indi = sys->N_edge_s; indi < sys->N_edge; indi++){
            nn += (yd2[indi] + yc[indi]) * (yd2[indi] + yc[indi]);
            nna += (yd2a[indi] + yca[indi]) * (yd2a[indi] + yca[indi]);
        }
        cout << endl;

        nn = sqrt(nn);
        nna = sqrt(nna);
        for (indi = sys->N_edge_s; indi < sys->N_edge; indi++){
            u0[sys->N_edge - bdn * sys->N_edge_s + indi - sys->N_edge_s].real = (yd2[indi] + yc[indi]) / nn;    // u0c is the other vector in u0
            u0a[sys->N_edge - bdn * sys->N_edge_s + indi - sys->N_edge_s].real = (yd2a[indi] + yca[indi]) / nna;    // u0ca
        }

        cout << " Time to generate u0d and u0c up to port" << sourcePort + 1 << " is " << (clock() - ts) * 1.0 / CLOCKS_PER_SEC << " s" << endl << endl;
        yd = (complex<double>*)malloc(sys->N_edge * sizeof(complex<double>));
        for (int id = 0; id < sys->N_edge; id++){
            yd[id] = yd2[id] - (1i)*(yd1[id]);
        }

        free(yd2); yd2 = NULL;
        free(yd1); yd1 = NULL;

        //sys->y = (complex<double>*)malloc(sys->N_edge*sizeof(complex<double>));
        for (indi = 0; indi < sys->N_edge; indi++){
            yd[indi] = yd[indi].real() + yc[indi] + (1i) * yd[indi].imag();
        }
        free(yc); yc = NULL;

        

#ifndef SKIP_VH
        // find the Vh eigenmodes
        status = find_Vh(sys, u0, u0a, sourcePort);

        free(m_new); m_new = NULL;

        for (indi = 0; indi < sys->nfreq; indi++){
            // this point's frequency

            if (sys->nfreq == 1) {    // to avoid (sys->nfreq - 1)
                freq = sys->freqStart * sys->freqUnit;
            }
            else {
                if (sys->freqScale == 1) {
                    freq = (sys->freqStart + indi * (sys->freqEnd - sys->freqStart) / (sys->nfreq - 1)) * sys->freqUnit;
                }
                else {
                    freq = sys->freqStart * sys->freqUnit * pow(sys->freqEnd / sys->freqStart, (indi * 1.0 / (sys->nfreq - 1)));
                }
            }
            
            // Vh = Vh - u0*((u0a'*A*u0)\(u0a'*A*Vh))
            lapack_complex_double* V_re2 = (lapack_complex_double*)malloc((sys->N_edge - bdn * sys->N_edge_s) * sys->leng_Vh * sizeof(lapack_complex_double));
            for (myint inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++) {    // A*Vh
                for (myint inde2 = 0; inde2 < sys->leng_Vh; inde2++) {
                    if (sys->markEdge[inde + sys->N_edge_s] != 0) {    // if this edge is inside the conductor
                        V_re2[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real = sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (-freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0
                            - 2 * M_PI * freq * sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * SIGMA;
                        V_re2[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag = sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (freq * 2 * M_PI) * SIGMA
                            - sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * (freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                    }
                    else {
                        V_re2[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real = sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (-freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                        V_re2[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag = -sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * (freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                    }
                }
            }

            y_re = (lapack_complex_double*)calloc(2 * sys->leng_Vh, sizeof(lapack_complex_double));    // u0a'*A*Vh
            status = matrix_multi('T', u0a, (sys->N_edge - bdn * sys->N_edge_s), 2, V_re2, (sys->N_edge - bdn * sys->N_edge_s), sys->leng_Vh, y_re);
            free(V_re2); V_re2 = NULL;

            tmp3 = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * 2, sizeof(lapack_complex_double));
            for (myint inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++) {    // A*u0
                for (myint inde2 = 0; inde2 < 2; inde2++) {
                    if (sys->markEdge[inde + sys->N_edge_s] != 0) {
                        tmp3[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real = u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (-freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0
                            - 2 * M_PI * freq * u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * SIGMA;
                        tmp3[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag = u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (freq * 2 * M_PI) * SIGMA
                            - u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * (freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                    }
                    else {
                        tmp3[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real = u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real * (-freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                        tmp3[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag = -u0[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag * (freq * freq * 4 * pow(M_PI, 2)) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0;
                    }
                }
            }

            tmp4 = (lapack_complex_double*)calloc(2 * 2, sizeof(lapack_complex_double));    // u0a'*A*u0
            status = matrix_multi('T', u0a, (sys->N_edge - bdn * sys->N_edge_s), 2, tmp3, (sys->N_edge - bdn * sys->N_edge_s), 2, tmp4);    // u0a'*A*u0
            ipiv = (lapack_int*)malloc(2 * sizeof(lapack_int));
            y_new = (lapack_complex_double*)calloc(2 * sys->leng_Vh, sizeof(lapack_complex_double));
            /*outfile.open("ma.txt", std::ofstream::out | std::ofstream::trunc);
            for (myint inde = 0; inde < 2; inde++){
            for (myint inde1 = 0; inde1 < 2; inde1++){
            outfile << tmp4[inde1 * 2 + inde].real << " " << tmp4[inde1 * 2 + inde].imag << " ";
            }
            outfile << endl;
            }*/
            info = LAPACKE_zcgesv(LAPACK_COL_MAJOR, 2, sys->leng_Vh, tmp4, 2, ipiv, y_re, 2, y_new, 2, &iter);    // (u0a'*A*u0)\(u0a'*A*V_re)
            free(ipiv); ipiv = NULL;
            free(y_re); y_re = NULL;
            free(tmp3); tmp3 = NULL;
            free(tmp4); tmp4 = NULL;
            m_new = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * sys->leng_Vh, sizeof(lapack_complex_double));
            status = matrix_multi('N', u0, (sys->N_edge - bdn * sys->N_edge_s), 2, y_new, 2, sys->leng_Vh, m_new);    // u0*((u0a'*A*u0)\(u0a'*A*V_re))
            free(y_new); y_new = NULL;
            Vh = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * (sys->leng_Vh), sizeof(lapack_complex_double));
            for (myint inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++) {
                for (myint inde2 = 0; inde2 < sys->leng_Vh; inde2++) {
                    Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real = sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real - m_new[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].real;
                    Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag = sys->Vh[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag - m_new[inde2 * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag;
                }
            }
            free(m_new); m_new = NULL;

            // Vh'*(A+C)*Vh
            tmp = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s) * sys->leng_Vh, sizeof(lapack_complex_double));
            for (j = 0; j < sys->leng_Vh; j++){    // calculate (A+C)*V_re1
                inde = 0;
                while (inde < sys->leng_S){
                    start = sys->SRowId[inde];
                    while (inde < sys->leng_S && sys->SRowId[inde] == start){

                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + sys->SRowId[inde]].real += sys->Sval[inde] * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + sys->SColId[inde]].real;
                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + sys->SRowId[inde]].imag += sys->Sval[inde] * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + sys->SColId[inde]].imag;


                        inde++;
                    }
                }

                for (inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++){
                    if (sys->markEdge[inde + sys->N_edge_s] != 0){
                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].real += -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].real
                            - freq * 2 * M_PI * SIGMA * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag;
                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag += -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag
                            + freq * 2 * M_PI * SIGMA * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].real;
                    }
                    else {
                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].real += -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].real;
                        tmp[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag += -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * Vh[j * (sys->N_edge - bdn * sys->N_edge_s) + inde].imag;
                    }
                }
            }


            m_h = (lapack_complex_double*)calloc(sys->leng_Vh * sys->leng_Vh, sizeof(lapack_complex_double));
            status = matrix_multi('T', Vh, (sys->N_edge - bdn * sys->N_edge_s), sys->leng_Vh, tmp, (sys->N_edge - bdn * sys->N_edge_s), sys->leng_Vh, m_h);    // V_re1'*(A+C)*V_re1


            rhs_h = (lapack_complex_double*)calloc(sys->leng_Vh * 1, sizeof(lapack_complex_double));
            J = (lapack_complex_double*)calloc(sys->N_edge - bdn * sys->N_edge_s, sizeof(lapack_complex_double));
            for (inde = sys->N_edge_s; inde < sys->N_edge; inde++){
                J[inde - sys->N_edge_s].imag = -sys->J[inde] * freq * 2 * M_PI;
            }
            status = matrix_multi('T', Vh, (sys->N_edge - bdn * sys->N_edge_s), sys->leng_Vh, J, (sys->N_edge - bdn * sys->N_edge_s), 1, rhs_h);    // -1i*omega*V_re1'*J
            free(J); J = NULL;

            /* V_re1'*A*u */
            //free(tmp);
            //tmp = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s), sizeof(lapack_complex_double));
            //for (inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++){
            //    if (sys->markEdge[inde + sys->N_edge_s] != 0){
            //        tmp[inde].real = -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * yd[inde + sys->N_edge_s].real() - freq * 2 * M_PI * SIGMA * yd[inde + sys->N_edge_s].imag() * sys->freqStart * sys->freqUnit / freq;
            //        tmp[inde].imag = freq * 2 * M_PI * SIGMA * yd[inde + sys->N_edge_s].real() - pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * yd[inde + sys->N_edge_s].imag() * sys->freqStart * sys->freqUnit / freq;
            //    }
            //    else {
            //        tmp[inde].real = -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * yd[inde + sys->N_edge_s].real();
            //        tmp[inde].imag = -pow(freq * 2 * M_PI, 2) * sys->stackEpsn[(inde + sys->N_edge_s + sys->N_edge_v) / (sys->N_edge_s + sys->N_edge_v)] * EPSILON0 * yd[inde + sys->N_edge_s].imag() * sys->freqStart * sys->freqUnit / freq;
            //    }
            //}
            //rhs_h0 = (lapack_complex_double*)calloc(sys->leng_Vh, sizeof(lapack_complex_double));
            //status = matrix_multi('T', sys->Vh, sys->N_edge - bdn * sys->N_edge_s, sys->leng_Vh, tmp, sys->N_edge - bdn * sys->N_edge_s, 1, rhs_h0);    // V_re1'*A*u
            //for (inde = 0; inde < sys->leng_Vh; inde++){
            //    rhs_h[inde].real = rhs_h[inde].real - rhs_h0[inde].real;
            //    rhs_h[inde].imag = rhs_h[inde].imag - rhs_h0[inde].imag;
            //}
            //free(rhs_h0); rhs_h0 = NULL;
            free(tmp); tmp = NULL;

            ipiv = (lapack_int*)malloc(sys->leng_Vh * sizeof(lapack_int));
            info1 = LAPACKE_zgesv(LAPACK_COL_MAJOR, sys->leng_Vh, 1, m_h, sys->leng_Vh, ipiv, rhs_h, sys->leng_Vh);// , y_h, sys->leng_Vh, &iter);    // yh is generated
            free(ipiv); ipiv = NULL;
            free(m_h); m_h = NULL;

            y_h = (lapack_complex_double*)calloc((sys->N_edge - bdn * sys->N_edge_s), sizeof(lapack_complex_double));
            status = matrix_multi('N', Vh, (sys->N_edge - bdn * sys->N_edge_s), sys->leng_Vh, rhs_h, sys->leng_Vh, 1, y_h);

            final_x = (lapack_complex_double*)malloc((sys->N_edge - bdn * sys->N_edge_s) * sizeof(lapack_complex_double));
            for (inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++){
                final_x[inde].real = yd[inde + sys->N_edge_s].real();// +y_h[inde].real;
                final_x[inde].imag = yd[inde + sys->N_edge_s].imag() *sys->freqStart * sys->freqUnit / freq;// +y_h[inde].imag;
            }

            free(y_h); y_h = NULL;
            free(rhs_h); rhs_h = NULL;

            xr = (complex<double>*)calloc(sys->N_edge - bdn * sys->N_edge_s, sizeof(complex<double>));
            status = reference1(sys, freq, sourcePort, sys->SRowId, sys->SColId, sys->Sval, xr);

            // Construct Z parameters
            for (inde = 0; inde < sys->numPorts; inde++){

                for (j = 0; j < sys->portEdge[inde].size(); j++){
                    if (sys->portEdge[inde][j] % (sys->N_edge_s + sys->N_edge_v) >= sys->N_edge_s){    // this edge is along z axis
                        inz = sys->portEdge[inde][j] / (sys->N_edge_s + sys->N_edge_v);
                        leng = sys->zn[inz + 1] - sys->zn[inz];
                    }
                    else if (sys->portEdge[inde][j] % (sys->N_edge_s + sys->N_edge_v) >= (sys->N_cell_y) * (sys->N_cell_x + 1)){    // this edge is along x axis
                        inx = ((sys->portEdge[inde][j] % (sys->N_edge_s + sys->N_edge_v)) - (sys->N_cell_y) * (sys->N_cell_x + 1)) / (sys->N_cell_y + 1);
                        leng = sys->xn[inx + 1] - sys->xn[inx];
                    }
                    else{    // this edge is along y axis
                        iny = (sys->portEdge[inde][j] % (sys->N_edge_s + sys->N_edge_v)) % sys->N_cell_y;
                        leng = sys->yn[iny + 1] - sys->yn[iny];
                    }

                    sys->x[indi * (sys->numPorts * sys->numPorts) + inde + sys->numPorts*xcol] = (sys->x[indi * (sys->numPorts * sys->numPorts) + inde + sys->numPorts*xcol].real() + final_x[sys->portEdge[inde][j] - sys->N_edge_s].real * leng / (sys->portArea[sourcePort] * (-sys->portCoor[sourcePort].portDirection))) + (1i)*(final_x[sys->portEdge[inde][j] - sys->N_edge_s].imag * leng / (sys->portArea[sourcePort] * (-sys->portCoor[sourcePort].portDirection)) + sys->x[indi * (sys->numPorts * sys->numPorts) + inde + sys->numPorts*xcol].imag());
                }
            }

            double err = 0, total_norm = 0, err0 = 0;
            for (inde = 0; inde < sys->N_edge - bdn * sys->N_edge_s; inde++){
                err += sqrt((xr[inde].real() - final_x[inde].real) * (xr[inde].real() - final_x[inde].real) + (xr[inde].imag() - final_x[inde].imag) * (xr[inde].imag() - final_x[inde].imag));
                err0 += sqrt((xr[inde].real() - yd[inde + sys->N_edge_s].real()) * (xr[inde].real() - yd[inde + sys->N_edge_s].real()) + (xr[inde].imag() - yd[inde + sys->N_edge_s].imag() *sys->freqStart * sys->freqUnit / freq) * (xr[inde].imag() - yd[inde + sys->N_edge_s].imag() *sys->freqStart * sys->freqUnit / freq));
                total_norm += sqrt((xr[inde].real()) * (xr[inde].real()) + (xr[inde].imag()) * (xr[inde].imag()));
            }
            cout << "Freq " << freq << " the total error is " << err / total_norm << endl;
            cout << "Freq " << freq << " the y0 total error is " << err0 / total_norm << endl;

            free(final_x); final_x = NULL;
            free(xr); xr = NULL;
            free(Vh); Vh = NULL;
        }
#endif


        free(yd); yd = NULL;
        free(sys->J); sys->J = NULL;
        free(v0daJ); v0daJ = NULL;
        free(y0d); y0d = NULL;

        sourcePort++;
        xcol++;
    }
        //cout << "Time after the third HYPRE is " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << endl;
        /* calculate the Vh part */
    /*cout << "markEdge is shown below: " << endl;
    for (indi = 0; indi < sys->N_edge; indi++){
        cout << sys->markEdge[indi] << " ";
    }
    cout << endl;*/

        // Solve system for x in (-omega^2 * D_eps + j * omega * D_sigma + S) * x = -j * omega * J
#ifndef SKIP_STIFF_REFERENCE

	sys->x.clear();
	for (indi = 0; indi < sys->nfreq; indi++) {
		if (sys->nfreq == 1) {    // to avoid (sys->nfreq - 1)
			freq = sys->freqStart * sys->freqUnit;
			}
		else {
			if (sys->freqScale == 1) {
				freq = (sys->freqStart + indi * (sys->freqEnd - sys->freqStart) / (sys->nfreq - 1)) * sys->freqUnit;
			}
			else {
				freq = sys->freqStart * sys->freqUnit * pow(sys->freqEnd / sys->freqStart, (indi * 1.0 / (sys->nfreq - 1)));
			}
		}
        
		for (int indj = 0; indj < sys->numPorts; indj++) {
			status = reference(sys, freq, indj, sys->SRowId, sys->SColId, sys->Sval);
		}
        cout << "Frequency " << freq << "'s z parameter matrix is shown below as" << endl;
        for (int indj = 0; indj < sys->numPorts; indj++){
            for (int indk = 0; indk < sys->numPorts; indk++){
                cout << sys->x[indi * (sys->numPorts * sys->numPorts) + indj * sys->numPorts + indk] << " ";
            }
            cout << endl;
        }
	}
    
#endif
    
    MPI_Finalize();

    /* Report the Z-parameters for V0 part */
    //if (sys->nfreq > 1){
    //    
    //    for (int id = 0; id < sys->nfreq; id++){
    //        double freq; // Initialize specific frequency (Hz)
    //        if (id == 0)
    //        {
    //            // First frequency in sweep
    //            freq = sys->freqStart * sys->freqUnit;

    //            // Report the saved result
    //            cout << "Z-parameters at frequency " << (sys->freqStart + id * (sys->freqEnd - sys->freqStart) / (sys->nfreq - 1)) * sys->freqUnit << " Hz:" << endl;
    //            for (indi = 0; indi < sys->numPorts; indi++) {
    //                for (j = 0; j < sys->numPorts; j++) {
    //                    Zresult = sys->x[j + indi*sys->numPorts];
    //                    cout << "  " << Zresult;
    //                }
    //                cout << endl;
    //            }
    //            continue;
    //        }
    //        else if (id == sys->nfreq - 1)
    //        {
    //            // Last frequency in sweep
    //            freq = sys->freqEnd * sys->freqUnit;
    //        }
    //        else
    //        {
    //            // All other frequencies in sweep
    //            if (sys->freqScale == 1)
    //            {
    //                // Linear interpolation of frequency sweep
    //                freq = (sys->freqStart + id * (sys->freqEnd - sys->freqStart) / (sys->nfreq - 1)) * sys->freqUnit;
    //            }
    //            else
    //            {
    //                // Logarithmic interpolation of frequency sweep
    //                freq = sys->freqStart * sys->freqUnit * pow(sys->freqEnd / sys->freqStart, (id * 1.0 / (sys->nfreq - 1))); // Should be most numerically stable calculated like this
    //                cout << "Log freq interp: " << freq << " Hz" << endl;
    //            }
    //        }

    //        // Report the results beyond the first and append to storage object
    //        cout << "Z-parameters at frequency " << freq << " Hz:" << endl;
    //        for (indi = 0; indi < sys->numPorts; indi++){
    //            for (j = 0; j < sys->numPorts; j++){
    //                Zresult = sys->x[j + indi*sys->numPorts].real() + (1i) * sys->x[j + indi*sys->numPorts].imag() * sys->freqStart * sys->freqUnit / freq;
    //                cout << "  " << Zresult;
    //                sys->x.push_back(Zresult);
    //            }
    //            cout << endl;
    //        }
    //    }
    //}
    //else{
    //    cout << "Z-parameters at single frequency " << (sys->freqStart) * sys->freqUnit << " Hz:" << endl;
    //    for (indi = 0; indi < sys->numPorts; indi++){
    //        for (j = 0; j < sys->numPorts; j++){
    //            Zresult = sys->x[j + indi*sys->numPorts];
    //            cout << Zresult << " ";
    //            //cout << Zresult.real() << "+ 1i* " << Zresult.imag() << " ";
    //        }
    //        cout << endl;
    //    }
    //}

    //sys->cindex.clear();
    //sys->acu_cnno.clear();


    free(sys->AdColId); sys->AdColId = NULL;
    free(sys->Adval); sys->Adval = NULL;
    free(sys->AdRowId); sys->AdRowId = NULL;
    free(sys->v0d1RowId); sys->v0d1RowId = NULL;
    free(sys->v0d1ColId); sys->v0d1ColId = NULL;
    //free(sys->v0d1ColIdo); sys->v0d1ColIdo = NULL;
    //free(sys->v0d1val); sys->v0d1val = NULL;
    free(sys->v0d1valo); sys->v0d1valo = NULL;
    //free(sys->v0d1aRowId); sys->v0d1aRowId = NULL;
    //free(sys->v0d1aColId); sys->v0d1aColId = NULL;
    //free(sys->v0d1aColIdo); sys->v0d1aColIdo = NULL;
    //free(sys->v0d1aval); sys->v0d1aval = NULL;
    free(sys->v0d1avalo); sys->v0d1avalo = NULL;
    free(sys->v0cRowId);  sys->v0cRowId = NULL;
    free(sys->v0cColId);  sys->v0cColId = NULL;
    //free(sys->v0cColIdo); sys->v0cColIdo = NULL;
    //free(sys->v0cval); sys->v0cval = NULL;
    free(sys->v0cvalo); sys->v0cvalo = NULL;
    //free(sys->v0caRowId); sys->v0caRowId = NULL;
    //free(sys->v0caColId); sys->v0caColId = NULL;
    //free(sys->v0caColIdo); sys->v0caColIdo = NULL;
    //free(sys->v0caval); sys->v0caval = NULL;
    free(sys->v0cavalo); sys->v0cavalo = NULL;
    free(sys->AcRowId); sys->AcRowId = NULL;
    free(sys->AcColId); sys->AcColId = NULL;
    free(sys->Acval); sys->Acval = NULL;
    free(sys->xn); sys->xn = NULL;
    free(sys->yn); sys->yn = NULL;
    free(sys->zn); sys->zn = NULL;
    sys->stackEpsn.clear();
    sys->portEdge.clear();

    mkl_sparse_destroy(V0dt);
    mkl_sparse_destroy(V0dat);
    mkl_sparse_destroy(V0ct);
    mkl_sparse_destroy(V0cat);

    //HYPRE_IJMatrixDestroy(ad);
    //HYPRE_IJMatrixDestroy(ac);

    return 0;
}

#ifndef SKIP_PARDISO
int pardisoSolve_c(fdtdMesh *sys, double *rhs, double *solution, int nodestart, int nodeend, int indstart, int indend){

    /*solve Ac system block by block*/
    int leng = nodeend - nodestart + 1;
    int status;
    double *a = (double*)malloc((indend - indstart + 1) * sizeof(double));
    int *ia = (int*)malloc((indend - indstart + 1) * sizeof(int));
    int *ja = (int*)malloc((indend - indstart + 1) * sizeof(int));

    for (int i = 0; i < (indend - indstart + 1); i++){
        a[i] = sys->Acval[indstart + i];
        ia[i] = sys->AcRowId[indstart + i] - sys->AcRowId[indstart];
        ja[i] = sys->AcColId[indstart + i] - sys->AcColId[indstart];
    }

    /* use pardiso to solve */
    {
         int *ia1 = (int*)malloc((leng + 1) * sizeof(int));
        /* status = COO2CSR_malloc(ia, ja, a, indstart - indend + 1, leng, ia1);
         if (status != 0)
             return status;*/
         int count = 0;
         int i = 0;
         int k = 0;
         int start;
         ia1[k] = 0;
         k++;
         while (i < (indend - indstart + 1)){
             start = ia[i];
             while (i < (indend - indstart + 1) && ia[i] == start) {
                 count++;
                 i++;
             }
             ia1[k] = (count);
             k++;
         }

         void *pt[64];
         int mtype;
         int iparm[64];
         double dparm[64];
         int maxfct, mnum, phase, error, solver;
         int num_process;   //number of processors
         int v0csin;
         int perm;
         int nrhs = 1;
         int msglvl = 0;    //print statistical information

         mtype = 11;    // real and not symmetric
         solver = 0;
         error = 0;
         maxfct = 1;    //maximum number of numerical factorizations
         mnum = 1;    //which factorization to use
         phase = 13;    //analysis

         pardisoinit(pt, &mtype, iparm);
         nrhs = 1;
         iparm[38] = 1;
         iparm[34] = 1;    //0-based indexing
         
         pardiso(pt, &maxfct, &mnum, &mtype, &phase, &leng, a, ia1, ja, &perm, &nrhs, iparm, &msglvl, rhs, solution, &error);
         free(ia1); ia1 = NULL;
    }

    /* use HYPRE to solve */
    /*{
        status = hypreSolve(sys, ia, ja, a, indend - indstart + 1, rhs, leng, solution);
    }*/
    
    
    free(a); a = NULL;
    free(ia); ia = NULL;
    free(ja); ja = NULL;
    
    

    return 0;
}
#endif

int matrixMul(vector<int> aRowId, vector<int> aColId, vector<double> aval, vector<int> bRowId, vector<int> bColId, vector<double> bval, vector<int> &cRowId, vector<int> &cColId, vector<double> &cval){
    //the first matrix is row by row, the second matrix is column by column

    int i = 0, j = 0;
    int flaga, flagb, k;
    int starta;
    double sum;

    flaga = aRowId[0];
    flagb = bColId[0];
    starta = 0;
    sum = 0;
    while (i < aRowId.size()){
        while (j < bColId.size() && bColId[j] == flagb && i < aRowId.size() && aRowId[i] == flaga){
            if (aColId[i] == bRowId[j]){
                sum += aval[i] * bval[j];
                j++;
                i++;
            }
            else if (aColId[i] < bRowId[j]){
                i++;
            }
            else if (aColId[i] > bRowId[j]){
                j++;
            }
        }
        if (sum != 0){
            cRowId.push_back(flaga);
            cColId.push_back(flagb);
            cval.push_back(sum);
            sum = 0;
        }
        if (i == aRowId.size()){
            if (j == bColId.size())
                break;
            else{
                i = starta;
                while (bColId[j] == flagb){
                    j++;
                    if (j == bColId.size()){
                        while (i < aRowId.size() && aRowId[i] == flaga){
                            i++;
                        }
                        starta = i;
                        if (i == aRowId.size())    //run all of the datas
                            break;
                        flaga = aRowId[i];
                        j = 0;
                        break;
                    }
                }
                flagb = bColId[j];
                continue;
            }
        }
        if (j == bColId.size()){
            while (i < aRowId.size() && aRowId[i] == flaga){
                i++;
            }
            starta = i;
            if (i == aRowId.size())    //run all of the datas
                break;
            flaga = aRowId[i];
            j = 0;
        }
        else{
            if (bColId[j] != flagb && aRowId[i] != flaga){
                flagb = bColId[j];
                i = starta;
            }
            else if (bColId[j] != flagb){
                flagb = bColId[j];
                i = starta;
            }
            else if (aRowId[i] != flaga){
                i = starta;
                while (bColId[j] == flagb){
                    j++;
                    if (j == bColId.size()){
                        while (i < aRowId.size() && aRowId[i] == flaga){
                            i++;
                        }
                        starta = i;
                        if (i == aRowId.size())    //run all of the datas
                            break;
                        flaga = aRowId[i];
                        j = 0;
                        break;
                    }
                }
                flagb = bColId[j];
            }

        }
    }

    return 0;
}

int COO2CSR(vector<int> &rowId, vector<int> &ColId, vector<double> &val){
    int i;
    vector<int> rowId2;
    int count, start;

    rowId2.push_back(0);
    count = 0;
    i = 0;
    while (i < rowId.size()){
        start = rowId[i];
        while (i < rowId.size() && rowId[i] == start) {
            count++;
            i++;
        }
        rowId2.push_back(count);
    }

    rowId.clear();
    rowId = rowId2;
    return 0;
}

int COO2CSR_malloc(myint *rowId, myint *ColId, double *val, myint totalnum, myint leng, myint *rowId1){    // totalnum is the total number of entries, leng is the row number
    int i;
    int *rowId2;
    int count, start;
    int k;

    rowId2 = (int*)malloc((leng + 1) * sizeof(int));
    count = 0;
    i = 0;
    k = 0;
    rowId2[k] = 0;
    k++;
    while (i < totalnum){
        start = rowId[i];
        while (i < totalnum && rowId[i] == start) {
            count++;
            i++;
        }
        rowId2[k] = (count);
        k++;
    }

    for (i = 0; i <= leng; i++){
        rowId1[i] = rowId2[i];
    }

    free(rowId2); rowId2 = NULL;
    return 0;
}

int mvMulti(vector<int> aRowId, vector<int> aColId, vector<double> aval, vector<int> &bRowId, vector<int> &bColId, vector<double> &bval, double *index_val, int size){
    //the same sequence in aColId and index
    double *v;
    int i;

    i = 0;
    v = (double*)calloc(size, sizeof(double));
    while (i < aColId.size()){
        v[aRowId[i]] += index_val[aColId[i]] * aval[i];
        i++;
    }
    for (i = 0; i < size; i++){
        if (abs(v[i]) > 1.e-1){
            bRowId.push_back(i);
            bColId.push_back(0);
            bval.push_back(v[i]);
        }
    }

    return 0;
}

int nodeAddAvgLarger(int *index, int size, int total_size, fdtdMesh *sys, int &num, int &leng, int* RowId, int *ColId, double *Val){    // Get the average V0d2 (around the conductor)
    /* orginal code */
    //int i, j;
    //double *v;
    //int inx, iny, inz;
    //vector<int> rowId, colId;
    //vector<double> val;

    //int *nodeset = (int*)calloc(sys->N_node, sizeof(int));
    //for (i = 0; i < size; i++){
    //    nodeset[index[i]] = 1;
    //}

    //v = (double*)calloc(total_size, sizeof(double));
    //inz = index[0] / sys->N_node_s;
    //inx = (index[0] - inz * sys->N_node_s) / (sys->N_cell_y + 1);
    //iny = index[0] % (sys->N_cell_y + 1);
    //if (iny == 0){
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny);
    //    colId.push_back(1);
    //    val.push_back(1 / (sys->yn[iny + 1] - sys->yn[iny]));
    //}
    //else if (iny == sys->N_cell_y){
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //    colId.push_back(1);
    //    val.push_back(-1 / (sys->yn[iny] - sys->yn[iny - 1]));
    //}
    //else{
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny);
    //    colId.push_back(1);
    //    val.push_back(2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //    colId.push_back(1);
    //    val.push_back(-2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //}

    //if (inx == 0){
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(1 / (sys->xn[inx + 1] - sys->xn[inx]));
    //}
    //else if (inx == sys->N_cell_x){
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(-1 / (sys->xn[inx] - sys->xn[inx - 1]));
    //}
    //else{
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(-2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx)+iny);
    //    colId.push_back(1);
    //    val.push_back(2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //}

    //if (inz == 0){
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(1 / (sys->zn[inz + 1] - sys->zn[inz]));
    //}
    //else if (inz == sys->N_cell_z){
    //    rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(-1 / (sys->zn[inz] - sys->zn[inz - 1]));
    //}
    //else{
    //    rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //    rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //    colId.push_back(1);
    //    val.push_back(-2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //}

    //for (i = 0; i < val.size(); i++){
    //    v[rowId[i]] = val[i];
    //}

    //while (!rowId.empty()){
    //    rowId.pop_back();
    //    colId.pop_back();
    //    val.pop_back();
    //}

    //int *visited;
    //stack<int> st;
    //double ratio;
    //visited = (int*)calloc(sys->N_node, sizeof(int));
    //int record;
    //int count;
    //st.push(index[0]);
    //visited[index[0]] = 1;
    //while (!st.empty()){
    //    record = 0;
    //    for (j = 0; j < sys->nodeEdge[st.top()].size(); j++){
    //        if ((sys->edgelink[sys->nodeEdge[st.top()][j].first * 2] != st.top() && visited[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2]] == 0) && (nodeset[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2]] == 1)){
    //            visited[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2]] = 1;

    //            inz = sys->edgelink[sys->nodeEdge[st.top()][j].first * 2] / sys->N_node_s;
    //            inx = (sys->edgelink[sys->nodeEdge[st.top()][j].first * 2] - inz * sys->N_node_s) / (sys->N_cell_y + 1);
    //            iny = sys->edgelink[sys->nodeEdge[st.top()][j].first * 2] % (sys->N_cell_y + 1);
    //            /*cout << "h" << endl;*/
    //            if (iny == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->yn[iny + 1] - sys->yn[iny]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->yn[iny + 1] - sys->yn[iny]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (iny == sys->N_cell_y){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->yn[iny] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny - 1 == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->yn[iny] - sys->yn[iny - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->yn[iny + 1] - sys->yn[iny - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1 == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->yn[iny + 1] - sys->yn[iny - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            if (inx == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->xn[inx + 1] - sys->xn[inx]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->xn[inx + 1] - sys->xn[inx]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (inx == sys->N_cell_x){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->xn[inx] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->xn[inx] - sys->xn[inx - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->xn[inx + 1] - sys->xn[inx - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx)+iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx)+iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->xn[inx + 1] - sys->xn[inx - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            if (inz == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->zn[inz + 1] - sys->zn[inz]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->zn[inz + 1] - sys->zn[inz]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (inz == sys->N_cell_z){
    //                rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->zn[inz] - sys->zn[inz - 1]));
    //                if ((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->zn[inz] - sys->zn[inz - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->zn[inz + 1] - sys->zn[inz - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //                if ((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->zn[inz + 1] - sys->zn[inz - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            for (i = 0; i < rowId.size(); i++){
    //                v[rowId[i]] = v[rowId[i]] + ratio * val[i];
    //            }
    //            while (!rowId.empty()){
    //                rowId.pop_back();
    //                colId.pop_back();
    //                val.pop_back();
    //            }

    //            st.push(sys->edgelink[sys->nodeEdge[st.top()][j].first * 2]);
    //            record = 1;

    //            break;
    //        }
    //        else if ((sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1] != st.top() && visited[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1]] == 0) && (nodeset[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1]] == 1)){
    //            visited[sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1]] = 1;

    //            inz = sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1] / sys->N_node_s;
    //            inx = (sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1] - inz * sys->N_node_s) / (sys->N_cell_y + 1);
    //            iny = sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1] % (sys->N_cell_y + 1);

    //            if (iny == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->yn[iny + 1] - sys->yn[iny]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->yn[iny + 1] - sys->yn[iny]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (iny == sys->N_cell_y){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->yn[iny] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*(sys->N_cell_y) + iny - 1 == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->yn[iny] - sys->yn[iny - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->yn[iny + 1] - sys->yn[iny - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->yn[iny + 1] - sys->yn[iny - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + inx*sys->N_cell_y + iny - 1 == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->yn[iny + 1] - sys->yn[iny - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            if (inx == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->xn[inx + 1] - sys->xn[inx]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->xn[inx + 1] - sys->xn[inx]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (inx == sys->N_cell_x){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->xn[inx] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->xn[inx] - sys->xn[inx - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx - 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->xn[inx + 1] - sys->xn[inx - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx)+iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->xn[inx + 1] - sys->xn[inx - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + (sys->N_cell_y)*(sys->N_cell_x + 1) + (sys->N_cell_y + 1)*(inx)+iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->xn[inx + 1] - sys->xn[inx - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            if (inz == 0){
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(1 / (sys->zn[inz + 1] - sys->zn[inz]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->zn[inz + 1] - sys->zn[inz]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else if (inz == sys->N_cell_z){
    //                rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-1 / (sys->zn[inz] - sys->zn[inz - 1]));
    //                if ((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->zn[inz] - sys->zn[inz - 1]) * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }
    //            else{
    //                rowId.push_back(inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //                if (inz*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = -(sys->zn[inz + 1] - sys->zn[inz - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //                rowId.push_back((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny);
    //                colId.push_back(1);
    //                val.push_back(-2 / (sys->zn[inz + 1] - sys->zn[inz - 1]));
    //                if ((inz - 1)*(sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + inx*(sys->N_cell_y + 1) + iny == sys->nodeEdge[st.top()][j].first){
    //                    ratio = (sys->zn[inz + 1] - sys->zn[inz - 1]) / 2 * v[sys->nodeEdge[st.top()][j].first];
    //                }
    //            }

    //            for (i = 0; i < rowId.size(); i++){
    //                v[rowId[i]] += ratio * val[i];
    //            }
    //            while (!rowId.empty()){
    //                rowId.pop_back();
    //                colId.pop_back();
    //                val.pop_back();
    //            }
    //            st.push(sys->edgelink[sys->nodeEdge[st.top()][j].first * 2 + 1]);
    //            record = 1;
    //            break;
    //        }
    //    }
    //    if (record == 0){
    //        st.pop();
    //    }
    //}


    //for (i = 0; i < total_size; i++){
    //    if (abs(v[i]) > 1e-5) {
    //        RowId[num] = (i);
    //        ColId[num] = (leng);
    //        Val[num] = (v[i]);
    //        num++;
    //    }
    //}
    //leng++;



    //free(visited);
    //visited = NULL;
    //free(v);
    //v = NULL;
    //free(nodeset);
    //nodeset = NULL;

    //return 0;

    /***************************************************************************************/
    int i, j;
    unordered_map<int, double> v;
    int inx, iny, inz;
    vector<int> rowId, colId;
    vector<double> val;

    int *nodeset = (int*)calloc(sys->N_node, sizeof(int));
    for (i = 0; i < size; i++){
        nodeset[index[i]] = 1;
    }

    inz = index[0] / sys->N_node_s;
    inx = (index[0] - inz * sys->N_node_s) / (sys->N_cell_y + 1);
    iny = index[0] % (sys->N_cell_y + 1);
    for (i = 0; i < sys->nodeEdgea[index[0]].size(); i++){
        v[sys->nodeEdgea[index[0]][i].first] = sys->nodeEdgea[index[0]][i].second;
    }


    int *visited;
    stack<int> st;
    double ratio;
    visited = (int*)calloc(sys->N_node, sizeof(int));
    int record;
    int count;
    int status;
    myint node1, node2;
    st.push(index[0]);
    visited[index[0]] = 1;
    while (!st.empty()){
        record = 0;
        for (j = 0; j < sys->nodeEdge[st.top()].size(); j++){
            status = compute_edgelink(sys, sys->nodeEdge[st.top()][j].first, node1, node2);

            if ((node1 != st.top() && visited[node1] == 0) && (nodeset[node1] == 1)){
                visited[node1] = 1;

                inz = node1 / sys->N_node_s;
                inx = (node1 - inz * sys->N_node_s) / (sys->N_cell_y + 1);
                iny = node1 % (sys->N_cell_y + 1);

                for (i = 0; i < sys->nodeEdgea[node1].size(); i++){
                    if (sys->nodeEdgea[node1][i].first == sys->nodeEdge[st.top()][j].first){
                        ratio = -1 / sys->nodeEdgea[node1][i].second * v[sys->nodeEdge[st.top()][j].first];
                        break;
                    }
                }

                for (i = 0; i < sys->nodeEdgea[node1].size(); i++){
                    if (v.find(sys->nodeEdgea[node1][i].first) == v.end()){
                        v[sys->nodeEdgea[node1][i].first] = ratio * sys->nodeEdgea[node1][i].second;
                    }
                    else{
                        v.erase(sys->nodeEdgea[node1][i].first);
                    }
                }


                st.push(node1);
                record = 1;

                break;
            }
            else if (node2 != st.top() && visited[node2] == 0 && (nodeset[node2] == 1)){
                visited[node2] = 1;

                inz = node2 / sys->N_node_s;
                inx = (node2 - inz * sys->N_node_s) / (sys->N_cell_y + 1);
                iny = node2 % (sys->N_cell_y + 1);

                for (i = 0; i < sys->nodeEdgea[node2].size(); i++){
                    if (sys->nodeEdgea[node2][i].first == sys->nodeEdge[st.top()][j].first){
                        ratio = -1 / sys->nodeEdgea[node2][i].second * v[sys->nodeEdge[st.top()][j].first];
                        break;
                    }
                }

                for (i = 0; i < sys->nodeEdgea[node2].size(); i++){
                    if (v.find(sys->nodeEdgea[node2][i].first) == v.end()){
                        v[sys->nodeEdgea[node2][i].first] = ratio * sys->nodeEdgea[node2][i].second;
                    }
                    else{
                        v.erase(sys->nodeEdgea[node2][i].first);
                    }
                }



                st.push(node2);
                record = 1;
                break;
            }
        }
        if (record == 0){
            st.pop();
        }
    }



    for (auto vi : v){
        if (abs(vi.second) > 1e-5){
            RowId[num] = vi.first;
            ColId[num] = leng;
            Val[num] = vi.second;
            num++;
        }
    }
    leng++;


    free(visited);
    visited = NULL;

    free(nodeset);
    nodeset = NULL;

    return 0;

}

#ifndef SKIP_PARDISO
//int solveV0dSystem(fdtdMesh *sys, double *dRhs, double *y0d, int leng_v0d1){
//    
//    clock_t t1 = clock();
//    /* A\b1 */
//    double *d = &(sys->Adval[0]);
//    int *id = &(sys->AdRowId1[0]);
//    int *jd = &(sys->AdColId[0]);
//    
//    void *ptd[64];
//    int mtyped;
//    int iparmd[64];
//    double dparmd[64];
//    int maxfctd, mnumd, phased, errord, solverd;
//    int num_processd;   //number of processors
//    int v0csin;
//    int permd;
//    int nrhs = 1;
//    int msglvld = 0;    //print statistical information
//
//    mtyped = 11;    // real and not symmetric
//    solverd = 0;
//    errord = 0;
//    maxfctd = 1;    //maximum number of numerical factorizations
//    mnumd = 1;    //which factorization to use
//    phased = 13;    //analysis
//
//    pardisoinit(ptd, &mtyped, iparmd);
//    nrhs = 1;
//    iparmd[38] = 1;
//    iparmd[34] = 1;    //0-based indexing
//    pardiso(ptd, &maxfctd, &mnumd, &mtyped, &phased, &leng_v0d1, d, id, jd, &permd, &nrhs, iparmd, &msglvld, dRhs, y0d, &errord);
//    cout << "Time to this point: " << (clock() - t1) * 1.0 / CLOCKS_PER_SEC << " s" << endl;
//
//    return 0;
//
//}
#endif

int merge_v0d1(fdtdMesh *sys, double block1_x, double block1_y, double block2_x, double block2_y, double block3_x, double block3_y, myint &v0d1num, myint &leng_v0d1, myint &v0d1anum, myint &leng_v0d1a, myint *map, double sideLen){

    int *visited;
    clock_t t1;
    double t, ta;
    double ratio;
    double startx, starty;    // the start coordinates of each block
    queue<int> st;    // dfs stack
    //vector<int> ind;
    int indsize, i;
    int indx, indy;
    int mark;
    int status;
    int indnum;
    int *markLayerNode = (int*)calloc(sys->N_node_s, sizeof(int));
    //int *markProSide = (int*)calloc(sys->N_node_s, sizeof(int));


    for (int i = 0; i < sys->numPorts; i++){
        for (int j = 0; j < sys->cdtNumNode[sys->portCoor[i].portCnd - 1]; j++){
            markLayerNode[sys->conductor[sys->portCoor[i].portCnd - 1].node[j] % (sys->N_node_s)] = 1;
        }
    }



    leng_v0d1 = 0;
    leng_v0d1a = 0;
    v0d1num = 0;
    v0d1anum = 0;
    
    /* First assign a larger number of storage, don't need to calculate the entries twice */
    //myint *v0d1RowId = (myint*)malloc(2 * sys->outedge * sizeof(myint));
    //myint *v0d1ColId = (myint*)malloc(2 * sys->outedge * sizeof(myint));
    //double *v0d1val = (double*)malloc(2 * sys->outedge * sizeof(double));
    ///*myint *v0d1aRowId = (myint*)malloc(sys->N_edge * sizeof(myint));
    //myint *v0d1aColId = (myint*)malloc(sys->N_edge * sizeof(myint));*/
    //double *v0d1aval = (double*)malloc(2 * sys->outedge * sizeof(double));

    
    int count = 1;    /* count which box it is */
    clock_t t2 = clock();
    unordered_map<myint, double> va, v;
    vector<set<myint>> node_group;
    set<myint> base;
    myint eno;
    double lx_avg, ly_avg, lz_avg;
    t = 0; ta = 0;
    myint node1, node2;
    int nodegs;   // node group #
    for (int iz = 1; iz < sys->nz; iz++){    // merge on each layer, not in the conductor
        visited = (int*)calloc(sys->nx * sys->ny, sizeof(int));
        for (int ix = 0; ix < sys->nx; ix++){
            for (int iy = 0; iy < sys->ny; iy++){
                if (visited[ix * (sys->N_cell_y + 1) + iy] == 0 && sys->markNode[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] == 0){
                    
                    
                    if (markLayerNode[ix * (sys->N_cell_y + 1) + iy] == 0 && sys->markProSide[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] == 0){    // this point is not visited and it is outside the conductor, not in the projection of the excited conductor
                        //if (!ind.empty())
                        //    ind.clear();
                        startx = sys->xn[ix];
                        starty = sys->yn[iy];
                        node_group.push_back(base);
                        nodegs = node_group.size() - 1;
                        node_group[nodegs].insert(iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy);
                        map[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] = count;
                        //ind.push_back(iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy);
                        st.push(ix * (sys->N_cell_y + 1) + iy);
                        visited[ix * (sys->N_cell_y + 1) + iy] = 1;
                        
                        while (!st.empty()){
                            indx = (st.front()) / (sys->N_cell_y + 1);
                            indy = st.front() % (sys->N_cell_y + 1);
                            if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + sys->N_cell_y + 1] == 0 && visited[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && sys->markProSide[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx + 1] - startx) >= 0 && (sys->xn[indx + 1] - startx) <= block1_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block1_y){    // this node is within the block area
                                        st.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy);

                                    }
                                }
                            }
                            if (indx != 0){    // it must have a left x edge, thus left x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - sys->N_cell_y - 1] == 0 && visited[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && sys->markProSide[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx - 1] - startx) >= 0 && (sys->xn[indx - 1] - startx) <= block1_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block1_y){    // this node is within the block area
                                        
                                        st.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy);

                                    }
                                }
                            }
                            if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + 1] == 0 && visited[indx * (sys->N_cell_y + 1) + indy + 1] == 0 && markLayerNode[indx * (sys->N_cell_y + 1) + indy + 1] == 0 && sys->markProSide[iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block1_x && (sys->yn[indy + 1] - starty) >= 0 && (sys->yn[indy + 1] - starty) <= block1_y){    // this node is within the block area
                                        
                                        st.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1);

                                    }
                                }
                            }
                            if (indy != 0){    // it must have a closer y edge, thus closer y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - 1] == 0 && visited[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && markLayerNode[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && sys->markProSide[iz * sys->N_node_s + (indx)* (sys->N_cell_y + 1) + indy - 1] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block1_x && (sys->yn[indy - 1] - starty) >= 0 && (sys->yn[indy - 1] - starty) <= block1_y){    // this node is within the block area

                                        st.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1);

                                    }
                                }
                            }
                            st.pop();
                        }

                        for (auto ndi : node_group[nodegs]){
                            indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
                            indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
                            status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
                            if (iz != 0){    // this node is not on the bottom plane
                                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (iz != sys->nz - 1){   // this node is not on the top plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != 0){    // this node is not on the left plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != sys->nx - 1){    // this node is not on the right plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != 0){    // this node is not on the front plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != sys->ny - 1){   // this node is not on the back plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                        }
                        
                        count++;
                    }

                    else if (markLayerNode[ix * (sys->N_cell_y + 1) + iy] == 1 && sys->markProSide[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] == 0){//&& sys->exciteCdtLayer[iz] == 1){    // this point is not visited and it is outside the conductor, in the projection of the excited conductor
                        startx = sys->xn[ix];
                        starty = sys->yn[iy];
                        
                        map[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] = count;
                        st.push(ix * (sys->N_cell_y + 1) + iy);
                        visited[ix * (sys->N_cell_y + 1) + iy] = 1;
                        node_group.push_back(base);
                        nodegs = node_group.size() - 1;
                        node_group[nodegs].insert(iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy);
                        while (!st.empty()){
                            mark = 0;
                            indx = (st.front()) / (sys->N_cell_y + 1);
                            indy = st.front() % (sys->N_cell_y + 1);

                            if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + sys->N_cell_y + 1] == 0 && visited[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx + 1) * (sys->N_cell_y + 1) + indy] == 1 && sys->markProSide[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx + 1] - startx) >= 0 && (sys->xn[indx + 1] - startx) <= block2_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block2_y){    // this node is within the block area

                                        st.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy);
                                    }
                                }
                            }

                            if (indx != 0){    // it must have a left x edge, thus left x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - sys->N_cell_y - 1] == 0 && visited[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx - 1) * (sys->N_cell_y + 1) + indy] == 1 && sys->markProSide[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx - 1] - startx) >= 0 && (sys->xn[indx - 1] - startx) <= block2_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block2_y){    // this node is within the block area
                                        
                                        st.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy);
                                    }
                                }
                            }
                            if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + 1] == 0 && visited[indx * (sys->N_cell_y + 1) + indy + 1] == 0 && markLayerNode[indx * (sys->N_cell_y + 1) + indy + 1] == 1 && sys->markProSide[iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block2_x && (sys->yn[indy + 1] - starty) >= 0 && (sys->yn[indy + 1] - starty) <= block2_y){    // this node is within the block area
                                        
                                        st.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1);
                                    }
                                }
                            }
                            if (indy != 0){    // it must have a closer y edge, thus closer y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - 1] == 0 && visited[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && markLayerNode[(indx)* (sys->N_cell_y + 1) + indy - 1] == 1 && sys->markProSide[iz * sys->N_node_s + (indx)* (sys->N_cell_y + 1) + indy - 1] == 0){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block2_x && (sys->yn[indy - 1] - starty) >= 0 && (sys->yn[indy - 1] - starty) <= block2_y){    // this node is within the block area
                                        
                                        st.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1);
                                    }
                                }
                            }
                            st.pop();
                        }

                        for (auto ndi : node_group[nodegs]){
                            indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
                            indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
                            status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
                            if (iz != 0){    // this node is not on the bottom plane
                                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (iz != sys->nz - 1){   // this node is not on the top plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != 0){    // this node is not on the left plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != sys->nx - 1){    // this node is not on the right plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != 0){    // this node is not on the front plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != sys->ny - 1){   // this node is not on the back plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                        }

                        
                        count++;

                    }
                    else{


                        startx = sys->xn[ix];
                        starty = sys->yn[iy];

                        map[iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy] = count;
                        st.push(ix * (sys->N_cell_y + 1) + iy);
                        visited[ix * (sys->N_cell_y + 1) + iy] = 1;
                        node_group.push_back(base);
                        nodegs = node_group.size() - 1;
                        node_group[nodegs].insert(iz * sys->N_node_s + ix * (sys->N_cell_y + 1) + iy);

                        while (!st.empty()){
                            mark = 0;
                            indx = (st.front()) / (sys->N_cell_y + 1);
                            indy = st.front() % (sys->N_cell_y + 1);

                            if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + sys->N_cell_y + 1] == 0 && visited[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && sys->markProSide[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] == 1){    // this node is in the sideLen
                                    if ((sys->xn[indx + 1] - startx) >= 0 && (sys->xn[indx + 1] - startx) <= block3_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block3_y){    // this node is within the block area
                                        st.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy);
                                    }
                                }
                            }
                            if (indx != 0){    // it must have a left x edge, thus left x node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - sys->N_cell_y - 1] == 0 && visited[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && sys->markProSide[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] == 1){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx - 1] - startx) >= 0 && (sys->xn[indx - 1] - startx) <= block3_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block3_y){    // this node is within the block area

                                        st.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                                        visited[(indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                                        map[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy);
                                    }
                                }
                            }
                            if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() + 1] == 0 && visited[indx * (sys->N_cell_y + 1) + indy + 1] == 0 && sys->markProSide[iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1] == 1){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block3_x && (sys->yn[indy + 1] - starty) >= 0 && (sys->yn[indy + 1] - starty) <= block3_y){    // this node is within the block area
                                        
                                        st.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1);
                                    }
                                }
                            }
                            if (indy != 0){    // it must have a closer y edge, thus closer y node
                                if (sys->markNode[iz * sys->N_node_s + st.front() - 1] == 0 && visited[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && sys->markProSide[iz * sys->N_node_s + (indx)* (sys->N_cell_y + 1) + indy - 1] == 1){    // this node is in dielectric and this node is not visited
                                    if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block3_x && (sys->yn[indy - 1] - starty) >= 0 && (sys->yn[indy - 1] - starty) <= block3_y){    // this node is within the block area
                                        
                                        st.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                                        visited[(indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                                        map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = count;
                                        node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1);
                                    }
                                }
                            }
                            st.pop();
                        }
                        for (auto ndi : node_group[nodegs]){
                            indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
                            indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
                            status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
                            if (iz != 0){    // this node is not on the bottom plane
                                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (iz != sys->nz - 1){   // this node is not on the top plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != 0){    // this node is not on the left plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indx != sys->nx - 1){    // this node is not on the right plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != 0){    // this node is not on the front plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                            if (indy != sys->ny - 1){   // this node is not on the back plane
                                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                                status = compute_edgelink(sys, eno, node1, node2);
                                if (node1 != ndi){
                                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                                else if (node2 != ndi){
                                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                        
                                        v0d1num++;
                                        v0d1anum++;
                                    }
                                }
                            }
                        }

                        
                        count++;
                    }
                }

            }
        }
        free(visited); visited = NULL;
    }
    /* V0d2 generation */
    int j;


    myint inz, inx, iny;
    myint iz, ix, iy;
    queue<myint> qu;
    visited = (int*)calloc(sys->N_node, sizeof(int));
    t2 = clock();
    for (i = 0; i < sys->numCdt; i++){
        //cout << sys->conductor[i].markPort << " ";
        if (sys->conductor[i].markPort == -1){
            continue;
        }
        else{
            mark = 0;    // if mark = 0 it means that no V0d2 for this conductor, leng_v0d doesn't increase by 1
            //v.clear();
            //va.clear();
            for (j = 0; j < sys->cdtNumNode[i]; j++)
            {
                map[sys->conductor[i].node[j]] = count;
                iz = sys->conductor[i].node[j] / sys->N_node_s;
                ix = (sys->conductor[i].node[j] % sys->N_node_s) / (sys->N_cell_y + 1);
                iy = (sys->conductor[i].node[j] % sys->N_node_s) % (sys->N_cell_y + 1);
                status = avg_length(sys, iz, iy, ix, lx_avg, ly_avg, lz_avg);
                if (iz != 0){    // this node is not on the bottom plane
                    eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy;    // the lower edge

                    if (sys->markEdge[eno] == 0){    // this edge is in the dielectric
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;
                    }
                }
                if (iz != sys->nz - 1){   // this node is not on the top plane
                    eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy;    // the upper edge

                    if (sys->markEdge[eno] == 0){
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;
                    }
                }
                if (ix != 0){    // this node is not on the left plane
                    eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (ix - 1) * (sys->N_cell_y + 1) + iy;    // the left edge

                    if (sys->markEdge[eno] == 0){
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;
                    }
                }
                if (ix != sys->nx - 1){    // this node is not on the right plane
                    eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + ix * (sys->N_cell_y + 1) + iy;    // the right edge

                    if (sys->markEdge[eno] == 0){
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;
                    }
                }
                if (iy != 0){    // this node is not on the front plane
                    eno = iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy - 1;    // the front edge

                    if (sys->markEdge[eno] == 0){
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;
                    }
                }
                if (iy != sys->ny - 1){   // this node is not on the back plane
                    eno = iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy;    // the back edge

                    if (sys->markEdge[eno] == 0){
                        
                        v0d1num++;
                        v0d1anum++;
                        mark = 1;    // mark = 1 means that V0d1 has entries for this conductor, leng_v0d will increase by 1
                    }
                }
            }
            if (mark == 1){
                
                count++;
            }
            
        }
    }
    
    sys->v0d1RowId = (myint*)malloc(v0d1num * sizeof(myint));
    sys->v0d1ColId = (myint*)malloc(v0d1num * sizeof(myint));
    sys->v0d1val = (double*)malloc(v0d1num * sizeof(double));
    sys->v0d1aval = (double*)malloc(v0d1anum * sizeof(double));

    double lx_whole_avg = 0;
    double ly_whole_avg = 0;
    double lz_whole_avg = 0;
    lx_whole_avg = (sys->xn[sys->nx - 1] - sys->xn[0]) / (sys->nx - 1);
    ly_whole_avg = (sys->yn[sys->ny - 1] - sys->yn[0]) / (sys->ny - 1);
    lz_whole_avg = (sys->zn[sys->nz - 1] - sys->zn[0]) / (sys->nz - 1);
    leng_v0d1 = 0;
    leng_v0d1a = 0;
    v0d1num = 0;
    v0d1anum = 0;
    for (nodegs = 0; nodegs < node_group.size(); nodegs++){
        for (auto ndi : node_group[nodegs]){
            iz = ndi / (sys->N_node_s);
            indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
            indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
            status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
            if (iz != 0){    // this node is not on the bottom plane
                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->zn[iz] - sys->zn[iz - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->zn[iz] - sys->zn[iz - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
            if (iz != sys->nz - 1){   // this node is not on the top plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->zn[iz + 1] - sys->zn[iz]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->zn[iz + 1] - sys->zn[iz]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
            if (indx != 0){    // this node is not on the left plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->xn[indx] - sys->xn[indx - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->xn[indx] - sys->xn[indx - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
            if (indx != sys->nx - 1){    // this node is not on the right plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->xn[indx + 1] - sys->xn[indx]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->xn[indx + 1] - sys->xn[indx]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
            if (indy != 0){    // this node is not on the front plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->yn[indy] - sys->yn[indy - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = -1 / (sys->yn[indy] - sys->yn[indy - 1]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = -lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
            if (indy != sys->ny - 1){   // this node is not on the back plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->yn[indy + 1] - sys->yn[indy]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0d1RowId[v0d1num] = eno;
                        sys->v0d1ColId[v0d1num] = leng_v0d1;
                        sys->v0d1val[v0d1num] = 1 / (sys->yn[indy + 1] - sys->yn[indy]);
                        v0d1num++;
                        sys->v0d1aval[v0d1anum] = lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0d1anum++;
                    }
                }
            }
        }
        leng_v0d1++;
        leng_v0d1a++;
    }
    for (i = 0; i < sys->numCdt; i++){
        if (sys->conductor[i].markPort == -1){
            continue;
        }
        for (j = 0; j < sys->cdtNumNode[i]; j++)
        {
            iz = sys->conductor[i].node[j] / sys->N_node_s;
            ix = (sys->conductor[i].node[j] % sys->N_node_s) / (sys->N_cell_y + 1);
            iy = (sys->conductor[i].node[j] % sys->N_node_s) % (sys->N_cell_y + 1);
            status = avg_length(sys, iz, iy, ix, lx_avg, ly_avg, lz_avg);
            if (iz != 0){    // this node is not on the bottom plane
                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy;    // the lower edge

                if (sys->markEdge[eno] == 0){    // this edge is in the dielectric
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = -1 / (sys->zn[iz] - sys->zn[iz - 1]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = -lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;
                }
            }
            if (iz != sys->nz - 1){   // this node is not on the top plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + ix * (sys->N_cell_y + 1) + iy;    // the upper edge

                if (sys->markEdge[eno] == 0){
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = 1 / (sys->zn[iz + 1] - sys->zn[iz]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;
                }
            }
            if (ix != 0){    // this node is not on the left plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (ix - 1) * (sys->N_cell_y + 1) + iy;    // the left edge

                if (sys->markEdge[eno] == 0){
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = -1 / (sys->xn[ix] - sys->xn[ix - 1]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = -ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;
                }
            }
            if (ix != sys->nx - 1){    // this node is not on the right plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + ix * (sys->N_cell_y + 1) + iy;    // the right edge

                if (sys->markEdge[eno] == 0){
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = 1 / (sys->xn[ix + 1] - sys->xn[ix]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;
                }
            }
            if (iy != 0){    // this node is not on the front plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy - 1;    // the front edge

                if (sys->markEdge[eno] == 0){
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = -1 / (sys->yn[iy] - sys->yn[iy - 1]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = -lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;
                }
            }
            if (iy != sys->ny - 1){   // this node is not on the back plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + ix * sys->N_cell_y + iy;    // the back edge

                if (sys->markEdge[eno] == 0){
                    sys->v0d1RowId[v0d1num] = eno;
                    sys->v0d1ColId[v0d1num] = leng_v0d1;
                    sys->v0d1val[v0d1num] = 1 / (sys->yn[iy + 1] - sys->yn[iy]);
                    v0d1num++;
                    sys->v0d1aval[v0d1anum] = lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                    v0d1anum++;
                    mark = 1;    // mark = 1 means that V0d1 has entries for this conductor, leng_v0d will increase by 1
                }
            }
        }
        if (mark == 1){
            leng_v0d1++;
            leng_v0d1a++;
        }
    }

    node_group.clear();

    return 1;



}

int setsideLen(int node, double sideLen, int *markLayerNode, int *markProSide, fdtdMesh *sys){
    queue<int> q;
    int *visited;
    int indx, indy;
    int mark;
    double startx, starty;

    if (sideLen == 0)
        return 0;

    visited = (int*)calloc(sys->N_node_s, sizeof(int));
    q.push(node);
    visited[node] = 1;
    startx = sys->xn[node / (sys->N_cell_y + 1)];
    starty = sys->yn[node % (sys->N_cell_y + 1)];
    // bfs
    while (!q.empty()){
        indx = q.front() / (sys->N_cell_y + 1);
        indy = q.front() % (sys->N_cell_y + 1);

        if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node

            if (visited[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx + 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                if (sqrt(pow((sys->xn[indx + 1] - startx), 2) + pow((sys->yn[indy] - starty), 2)) <= sideLen){    // this node is within the block area
                    q.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                    visited[(indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                    markProSide[(indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                }
            }
        }

        if (indx != 0){    // it must have a left x edge, thus left x node
            if (visited[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && markLayerNode[(indx - 1) * (sys->N_cell_y + 1) + indy] == 0){    // this node is in dielectric and this node is not visited
                if (sqrt(pow((sys->xn[indx - 1] - startx), 2) + pow((sys->yn[indy] - starty), 2)) <= sideLen){    // this node is within the block area
                    q.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                    visited[(indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                    markProSide[(indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                }
            }
        }

        if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
            if (visited[indx * (sys->N_cell_y + 1) + indy + 1] == 0 && markLayerNode[indx * (sys->N_cell_y + 1) + indy + 1] == 0){    // this node is in dielectric and this node is not visited
                if (sqrt(pow((sys->xn[indx] - startx), 2) + pow((sys->yn[indy + 1] - starty), 2)) <= sideLen){    // this node is within the block area
                    q.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                    visited[(indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                    markProSide[(indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                }
            }
        }

        if (indy != 0){    // it must have a closer y edge, thus closer y node
            if (visited[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && markLayerNode[(indx)* (sys->N_cell_y + 1) + indy - 1] == 0){    // this node is in dielectric and this node is not visited
                if (sqrt(pow((sys->xn[indx] - startx), 2) + pow((sys->yn[indy - 1] - starty), 2)) <= sideLen){    // this node is within the block area
                    q.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                    visited[(indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                    markProSide[(indx)* (sys->N_cell_y + 1) + indy - 1] = 1;
                }
            }
        }

        q.pop();
    }

    free(visited); visited = NULL;

    return 0;
}

int merge_v0c(fdtdMesh *sys, double block_x, double block_y, double block2_x, double block2_y, myint &v0cnum, myint &leng_v0c, myint &v0canum, myint &leng_v0ca, myint *map){

    int *visited;
    double ratio;
    double startx, starty;    // the start coordinates of each block
    queue<int> st;    // dfs stack
    //vector<int> ind;
    int indsize;
    int indx, indy;
    int mark;
    int status;
    int markcond;
    int count = 0;
    leng_v0c = 0;
    leng_v0ca = 0;
    v0cnum = 0;
    v0canum = 0;
    int indnum;
    int ix, iy, iz;
    int n;
    int i, j;
    int map_count = 1;
    myint eno;
    double lx_avg, ly_avg, lz_avg;
    myint node1, node2;


    unordered_map<myint, double> v, va;
    visited = (int*)calloc(sys->N_node, sizeof(int));
    vector<set<myint>> node_group;
    set<myint> base;
    int nodegs;

    for (int ic = 0; ic < sys->numCdt; ic++){
        
        if (sys->conductor[ic].markPort <= 0){    // not excited conductors
            markcond = ic + 1; 
            //visited = (int*)calloc(sys->N_node, sizeof(int));
            n = sys->cdtNumNode[ic] - 1;
            if (sys->conductor[ic].markPort <= -1)
                n = sys->cdtNumNode[ic];
            for (int jc = 0; jc < n; jc++){
                if (visited[sys->conductor[ic].node[jc]] == 0 && sys->conductor[ic].node[jc] >= sys->N_node_s){
                    

                    //if (!ind.empty())
                    //    ind.clear();
                    iz = sys->conductor[ic].node[jc] / (sys->N_node_s);
                    ix = (sys->conductor[ic].node[jc] - iz * sys->N_node_s) / (sys->N_cell_y + 1);
                    iy = sys->conductor[ic].node[jc] % (sys->N_cell_y + 1);
                    startx = sys->xn[ix];
                    starty = sys->yn[iy];
                    //ind.push_back(sys->conductor[ic].node[jc]);
                    st.push(ix * (sys->N_cell_y + 1) + iy);
                    visited[sys->conductor[ic].node[jc]] = 1;
                    map[sys->conductor[ic].node[jc]] = map_count;
                    node_group.push_back(base);
                    nodegs = node_group.size() - 1;
                    node_group[nodegs].insert(sys->conductor[ic].node[jc]);
                    while (!st.empty()){

                        mark = 0;
                        indx = (st.front()) / (sys->N_cell_y + 1);
                        indy = st.front() % (sys->N_cell_y + 1);
                        if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (sys->N_cell_y + 1) * indx + indy] == markcond && visited[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && (iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx + 1] - startx) >= 0 && (sys->xn[indx + 1] - startx) <= block_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block_y){    // this node is within the block area

                                    st.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                                    visited[iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                                    map[iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }
                        if (indx != 0){    // it must have a left x edge, thus left x node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (sys->N_cell_y + 1) * (indx - 1) + indy] == markcond && visited[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && (iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx - 1] - startx) >= 0 && (sys->xn[indx - 1] - startx) <= block_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block_y){    // this node is within the block area

                                    st.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                                    visited[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                                    map[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }

                        if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * indx + indy] == markcond && visited[iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1] == 0 && (iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1 != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block_x && (sys->yn[indy + 1] - starty) >= 0 && (sys->yn[indy + 1] - starty) <= block_y){    // this node is within the block area

                                    st.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                                    visited[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                                    map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }

                        if (indy != 0){    // it must have a closer y edge, thus closer y node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * indx + indy - 1] == markcond && visited[iz * sys->N_node_s + (indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && (iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy - 1 != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block_x && (sys->yn[indy - 1] - starty) >= 0 && (sys->yn[indy - 1] - starty) <= block_y){    // this node is within the block area

                                    st.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                                    visited[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                                    map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }

                        //if (mark == 0){
                        st.pop();
                        //}
                    }
                    for (auto ndi : node_group[nodegs]){
                        indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
                        indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
                        status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
                        if (iz != 0){    // this node is not on the bottom plane
                            eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (iz != sys->nz - 1){   // this node is not on the top plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){

                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indx != 0){    // this node is not on the left plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indx != sys->nx - 1){    // this node is not on the right plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indy != 0){    // this node is not on the front plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indy != sys->ny - 1){   // this node is not on the back plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                    }

                    map_count++;

                }
            }
            if (leng_v0c > sys->acu_cnno.back())
                sys->acu_cnno.push_back(leng_v0c);

            //free(visited); visited = NULL;
        }
        else{

            markcond = ic + 1;
            
            //visited = (int*)calloc(sys->N_node, sizeof(int));
            n = sys->cdtNumNode[ic] - 1;
            for (int jc = 0; jc < n; jc++){
                if (visited[sys->conductor[ic].node[jc]] == 0 && sys->conductor[ic].node[jc] >= sys->N_node_s){
                    
                    iz = sys->conductor[ic].node[jc] / (sys->N_node_s);
                    ix = (sys->conductor[ic].node[jc] - iz * sys->N_node_s) / (sys->N_cell_y + 1);
                    iy = sys->conductor[ic].node[jc] % (sys->N_cell_y + 1);
                    startx = sys->xn[ix];
                    starty = sys->yn[iy];
                    //ind.push_back(sys->conductor[ic].node[jc]);
                    st.push(ix * (sys->N_cell_y + 1) + iy);
                    visited[sys->conductor[ic].node[jc]] = 1;
                    map[sys->conductor[ic].node[jc]] = map_count;
                    node_group.push_back(base);
                    nodegs = node_group.size() - 1;
                    node_group[nodegs].insert(sys->conductor[ic].node[jc]);

                    while (!st.empty()){

                        mark = 0;
                        indx = (st.front()) / (sys->N_cell_y + 1);
                        indy = st.front() % (sys->N_cell_y + 1);

                        if (indx != sys->nx - 1){    // it must have a right x edge, thus right x node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (sys->N_cell_y + 1) * indx + indy] == markcond && visited[iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy] == 0 && (iz * sys->N_node_s + (indx + 1) * (sys->N_cell_y + 1) + indy != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx + 1] - startx) >= 0 && (sys->xn[indx + 1] - startx) <= block2_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block2_y){    // this node is within the block area

                                    st.push((indx + 1)*(sys->N_cell_y + 1) + indy);
                                    visited[iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy] = 1;
                                    map[iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx + 1)*(sys->N_cell_y + 1) + indy);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }
                        if (indx != 0){    // it must have a left x edge, thus left x node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (sys->N_cell_y + 1) * (indx - 1) + indy] == markcond && visited[iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy] == 0 && (iz * sys->N_node_s + (indx - 1) * (sys->N_cell_y + 1) + indy != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx - 1] - startx) >= 0 && (sys->xn[indx - 1] - startx) <= block2_x && (sys->yn[indy] - starty) >= 0 && (sys->yn[indy] - starty) <= block2_y){    // this node is within the block area

                                    st.push((indx - 1)*(sys->N_cell_y + 1) + indy);
                                    visited[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = 1;
                                    map[iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx - 1)*(sys->N_cell_y + 1) + indy);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }
                        if (indy != sys->ny - 1){    // it must have a farther y edge, thus farther y node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * indx + indy] == markcond && visited[iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1] == 0 && (iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy + 1 != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block2_x && (sys->yn[indy + 1] - starty) >= 0 && (sys->yn[indy + 1] - starty) <= block2_y){    // this node is within the block area

                                    st.push((indx)*(sys->N_cell_y + 1) + indy + 1);
                                    visited[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = 1;
                                    map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy + 1);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }
                        if (indy != 0){    // it must have a closer y edge, thus closer y node
                            if (sys->markEdge[iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * indx + indy - 1] == markcond && visited[iz * sys->N_node_s + (indx)* (sys->N_cell_y + 1) + indy - 1] == 0 && (iz * sys->N_node_s + indx * (sys->N_cell_y + 1) + indy - 1 != sys->conductor[ic].node[sys->cdtNumNode[ic] - 1] || sys->conductor[ic].markPort == -1)){    // this node is in conductor and this node is not visited
                                if ((sys->xn[indx] - startx) >= 0 && (sys->xn[indx] - startx) <= block2_x && (sys->yn[indy - 1] - starty) >= 0 && (sys->yn[indy - 1] - starty) <= block2_y){    // this node is within the block area
                                    st.push((indx)*(sys->N_cell_y + 1) + indy - 1);
                                    visited[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = 1;
                                    map[iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1] = map_count;
                                    node_group[nodegs].insert(iz * sys->N_node_s + (indx)*(sys->N_cell_y + 1) + indy - 1);
                                    //mark = 1;

                                    //continue;
                                }
                            }
                        }
                        //if (mark == 0){

                        st.pop();

                        //}
                    }
                    for (auto ndi : node_group[nodegs]){
                        indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
                        indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
                        status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
                        if (iz != 0){    // this node is not on the bottom plane
                            eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (iz != sys->nz - 1){   // this node is not on the top plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indx != 0){    // this node is not on the left plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indx != sys->nx - 1){    // this node is not on the right plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indy != 0){    // this node is not on the front plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                        if (indy != sys->ny - 1){   // this node is not on the back plane
                            eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                            status = compute_edgelink(sys, eno, node1, node2);
                            if (node1 != ndi){
                                if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                            else if (node2 != ndi){
                                if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                                    
                                    v0cnum++;
                                    v0canum++;
                                }
                            }
                        }
                    }
                    map_count++;


                }
            }

            if (leng_v0c > sys->acu_cnno.back())
                sys->acu_cnno.push_back(leng_v0c);

            //free(visited); visited = NULL;

        }
    }
    
    sys->v0cRowId = (myint*)malloc(v0cnum * sizeof(myint));
    sys->v0cColId = (myint*)malloc(v0cnum * sizeof(myint));
    sys->v0cval = (double*)malloc(v0cnum * sizeof(double));
    //sys->v0caRowId = (myint*)malloc(v0canum * sizeof(myint));
    //sys->v0caColId = (myint*)malloc(v0canum * sizeof(myint));
    sys->v0caval = (double*)malloc(v0canum * sizeof(double));
    v0cnum = 0;
    v0canum = 0;
    leng_v0c = 0;
    leng_v0ca = 0;
    double lx_whole_avg = 0;
    double ly_whole_avg = 0;
    double lz_whole_avg = 0;
    lx_whole_avg = (sys->xn[sys->nx - 1] - sys->xn[0]) / (sys->nx - 1);
    ly_whole_avg = (sys->yn[sys->ny - 1] - sys->yn[0]) / (sys->ny - 1);
    lz_whole_avg = (sys->zn[sys->nz - 1] - sys->zn[0]) / (sys->nz - 1);

    for (nodegs = 0; nodegs < node_group.size(); nodegs++){
        for (auto ndi : node_group[nodegs]){
            iz = ndi / (sys->N_node_s);
            indx = (ndi % sys->N_node_s) / (sys->N_cell_y + 1);
            indy = (ndi % sys->N_node_s) % (sys->N_cell_y + 1);
            status = avg_length(sys, iz, indy, indx, lx_avg, ly_avg, lz_avg);
            if (iz != 0){    // this node is not on the bottom plane
                eno = (iz - 1) * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the lower edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->zn[iz] - sys->zn[iz - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->zn[iz] - sys->zn[iz - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
            if (iz != sys->nz - 1){   // this node is not on the top plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_edge_s + indx * (sys->N_cell_y + 1) + indy;    // the upper edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->zn[iz + 1] - sys->zn[iz]);
                        v0cnum++;
                        sys->v0caval[v0canum] = lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->zn[iz + 1] - sys->zn[iz]);
                        v0cnum++;
                        sys->v0caval[v0canum] = lx_avg * ly_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
            if (indx != 0){    // this node is not on the left plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + (indx - 1) * (sys->N_cell_y + 1) + indy;    // the left edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->xn[indx] - sys->xn[indx - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->xn[indx] - sys->xn[indx - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
            if (indx != sys->nx - 1){    // this node is not on the right plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + sys->N_cell_y * (sys->N_cell_x + 1) + indx * (sys->N_cell_y + 1) + indy;    // the right edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->xn[indx + 1] - sys->xn[indx]);
                        v0cnum++;
                        sys->v0caval[v0canum] = ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->xn[indx + 1] - sys->xn[indx]);
                        v0cnum++;
                        sys->v0caval[v0canum] = ly_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
            if (indy != 0){    // this node is not on the front plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy - 1;    // the front edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->yn[indy] - sys->yn[indy - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = -1 / (sys->yn[indy] - sys->yn[indy - 1]);
                        v0cnum++;
                        sys->v0caval[v0canum] = -lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
            if (indy != sys->ny - 1){   // this node is not on the back plane
                eno = iz * (sys->N_edge_s + sys->N_edge_v) + indx * sys->N_cell_y + indy;    // the back edge
                status = compute_edgelink(sys, eno, node1, node2);
                if (node1 != ndi){
                    if (node_group[nodegs].find(node1) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->yn[indy + 1] - sys->yn[indy]);
                        v0cnum++;
                        sys->v0caval[v0canum] = lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
                else if (node2 != ndi){
                    if (node_group[nodegs].find(node2) == node_group[nodegs].end()){
                        sys->v0cRowId[v0cnum] = eno;
                        sys->v0cColId[v0cnum] = leng_v0c;
                        sys->v0cval[v0cnum] = 1 / (sys->yn[indy + 1] - sys->yn[indy]);
                        v0cnum++;
                        sys->v0caval[v0canum] = lx_avg * lz_avg / (lx_whole_avg * ly_whole_avg * lz_whole_avg);
                        v0canum++;
                    }
                }
            }
        }

        leng_v0c++;
        leng_v0ca++;
    }

    /*cout << "Number of columns of V0c is " << leng_v0c << " and number of non-zeros of V0c is " << v0cnum << endl;
    cout << endl;*/
   
    

    return 1;

}

int avg_length(fdtdMesh *sys, int iz, int iy, int ix, double &lx, double &ly, double &lz){    // given a node, we can know its averaged lengths along x, y, z directions
    if (iz == 0){
        lz = sys->zn[1] - sys->zn[0];
    }
    else if (iz == sys->nz - 1){
        lz = sys->zn[iz] - sys->zn[iz - 1];
    }
    else {
        lz = (sys->zn[iz + 1] - sys->zn[iz - 1]) / 2;
    }

    if (iy == 0){
        ly = sys->yn[1] - sys->yn[0];
    }
    else if (iy == sys->ny - 1){
        ly = sys->yn[iy] - sys->yn[iy - 1];
    }
    else {
        ly = (sys->yn[iy + 1] - sys->yn[iy - 1]) / 2;
    }

    if (ix == 0){
        lx = sys->xn[1] - sys->xn[0];
    }
    else if (ix == sys->nx - 1){
        lx = sys->xn[ix] - sys->xn[ix - 1];
    }
    else {
        lx = (sys->xn[ix + 1] - sys->xn[ix - 1]) / 2;
    }

    return 0;
}
