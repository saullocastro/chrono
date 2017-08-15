#include <mpi.h>
#include <omp.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <memory>
#include <iostream>

#include "../../chrono_distributed/collision/ChCollisionModelDistributed.h"
#include "../../chrono_distributed/physics/ChSystemDistributed.h"
#include "chrono/utils/ChUtilsCreators.h"
#include "chrono/utils/ChUtilsInputOutput.h"

#include "chrono_parallel/solver/ChIterativeSolverParallel.h"

#define MASTER 0

using namespace chrono;
using namespace chrono::collision;

int my_rank;
int num_ranks;

int num_threads;

// Tilt angle (about global Y axis) of the container.
double tilt_angle = 0;

// Number of balls: (2 * count_X + 1) * (2 * count_Y + 1)
int count_X = 10;  // 10  // 20
int count_Y = 10;  // 10  // 4

// Material properties (same on bin and balls)
float Y = 2e6f;
float mu = 0.4f;
float cr = 0.4f;

void print(std::string msg) {
    if (my_rank == MASTER) {
        std::cout << msg;
    }
}

void Monitor(chrono::ChSystemParallel* system) {
    double TIME = system->GetChTime();
    double STEP = system->GetTimerStep();
    double BROD = system->GetTimerCollisionBroad();

    double B1 = system->data_manager->system_timer.GetTime("B1");
    double B2 = system->data_manager->system_timer.GetTime("B2");
    double B3 = system->data_manager->system_timer.GetTime("B3");
    double B4 = system->data_manager->system_timer.GetTime("B4");
    double B5 = system->data_manager->system_timer.GetTime("B5");

    double A = system->data_manager->system_timer.GetTime("A");

    double NARR = system->GetTimerCollisionNarrow();
    double SOLVER = system->GetTimerSolver();
    double UPDT = system->GetTimerUpdate();
    double SEND = system->data_manager->system_timer.GetTime("Send");
    double RECV = system->data_manager->system_timer.GetTime("Recv");
    double EXCH = system->data_manager->system_timer.GetTime("Exchange");
    int BODS = system->GetNbodies();
    int CNTC = system->GetNcontacts();
    double RESID = 0;
    int REQ_ITS = 0;
    if (chrono::ChSystemParallel* parallel_sys = dynamic_cast<chrono::ChSystemParallel*>(system)) {
        RESID = std::static_pointer_cast<chrono::ChIterativeSolverParallel>(system->GetSolver())->GetResidual();
        REQ_ITS =
            std::static_pointer_cast<chrono::ChIterativeSolverParallel>(system->GetSolver())->GetTotalIterations();
    }

    printf(
        "%d|   %8.5f | %7.4f | E%7.4f | S%7.4f | R%7.4f | B%7.4f | B1%7.4f | B2%7.4f | B3%7.4f | B4%7.4f | B5%7.4f | "
        "A%7.4f | N%7.4f | %7.4f | %7.4f | %7d | %7d | %7d | %7.4f\n",
        my_rank, TIME, STEP, EXCH, SEND, RECV, BROD, B1, B2, B3, B4, B5, A, NARR, SOLVER, UPDT, BODS, CNTC, REQ_ITS,
        RESID);
}

void OutputData(ChSystemDistributed* sys, int out_frame, double time) {
    std::string filedir;
    if (num_ranks == 1) {
        filedir = "../reference";
    } else {
        filedir = "../granular";
    }
    std::string filename = "data" + std::to_string(out_frame);
    sys->WriteCSV(filedir, filename);

    std::cout << "time = " << time << std::flush << std::endl;
}

// -----------------------------------------------------------------------------
// Create a bin consisting of five boxes attached to the ground.
// -----------------------------------------------------------------------------
void AddContainer(ChSystemDistributed* sys) {
    // IDs for the two bodies
    int binId = -200;

    // Create a common material
    auto mat = std::make_shared<ChMaterialSurfaceSMC>();
    mat->SetYoungModulus(Y);
    mat->SetFriction(mu);
    mat->SetRestitution(cr);

    // Create the containing bin (4 x 4 x 1)
    auto bin = std::make_shared<ChBody>(std::make_shared<ChCollisionModelDistributed>(), ChMaterialSurface::SMC);
    bin->SetMaterialSurface(mat);
    bin->SetIdentifier(binId);
    bin->SetMass(1);
    bin->SetPos(ChVector<>(0, 0, 0));
    bin->SetRot(Q_from_AngY(tilt_angle));
    bin->SetCollide(true);
    bin->SetBodyFixed(true);

    ChVector<> hdim(4, 4, 15);  // 5,5,10
    double hthick = 0.1;

    bin->GetCollisionModel()->ClearModel();
    utils::AddBoxGeometry(bin.get(), ChVector<>(hdim.x(), hdim.y(), hthick), ChVector<>(0, 0, -hthick));
    utils::AddBoxGeometry(bin.get(), ChVector<>(hthick, hdim.y(), hdim.z()),
                          ChVector<>(-hdim.x() - hthick, 0, hdim.z()));
    utils::AddBoxGeometry(bin.get(), ChVector<>(hthick, hdim.y(), hdim.z()),
                          ChVector<>(hdim.x() + hthick, 0, hdim.z()));
    utils::AddBoxGeometry(bin.get(), ChVector<>(hdim.x(), hthick, hdim.z()),
                          ChVector<>(0, -hdim.y() - hthick, hdim.z()));
    utils::AddBoxGeometry(bin.get(), ChVector<>(hdim.x(), hthick, hdim.z()),
                          ChVector<>(0, hdim.y() + hthick, hdim.z()));
    bin->GetCollisionModel()->BuildModel();

    sys->AddBody(bin);
}

// -----------------------------------------------------------------------------
// Create the falling spherical objects in a uniform rectangular grid.
// -----------------------------------------------------------------------------
void AddFallingBalls(ChSystemDistributed* sys) {
    // Common material
    auto ballMat = std::make_shared<ChMaterialSurfaceSMC>();
    ballMat->SetYoungModulus(Y);
    ballMat->SetFriction(mu);
    ballMat->SetRestitution(cr);
    ballMat->SetAdhesion(0);  // Magnitude of the adhesion in Constant adhesion model

    // Create the falling balls
    int ballId = 0;
    double mass = 1;
    double radius = 0.15;
    ChVector<> inertia = (2.0 / 5.0) * mass * radius * radius * ChVector<>(1, 1, 1);

    // TODO generate randomly. Need to seed though.
    for (double z = 10; z < 15; z += 0.35) {
        for (int ix = -count_X; ix <= count_X; ix++) {
            for (int iy = -count_Y; iy <= count_Y; iy++) {
                ChVector<> pos(0.35 * ix, 0.35 * iy, z);  //.4*ix, .4*iy,z

                auto ball =
                    std::make_shared<ChBody>(std::make_shared<ChCollisionModelDistributed>(), ChMaterialSurface::SMC);
                ball->SetMaterialSurface(ballMat);

                ball->SetIdentifier(ballId++);
                ball->SetMass(mass);
                ball->SetInertiaXX(inertia);
                ball->SetPos(pos);
                ball->SetRot(ChQuaternion<>(1, 0, 0, 0));
                ball->SetBodyFixed(false);
                ball->SetCollide(true);

                ball->GetCollisionModel()->ClearModel();
                utils::AddSphereGeometry(ball.get(), radius);
                ball->GetCollisionModel()->BuildModel();

                sys->AddBody(ball);
            }
        }
    }
}

void AddBigBall(ChSystemDistributed* my_sys) {
    double ball_radius = 1.0;
    auto ballMat = std::make_shared<ChMaterialSurfaceSMC>();
    ballMat->SetYoungModulus(Y);
    ballMat->SetFriction(mu);
    ballMat->SetRestitution(cr);
    ballMat->SetAdhesion(0);  // Magnitude of the adhesion in Constant adhesion model

    double mass = 10;
    ChVector<> inertia = (2.0 / 5.0) * mass * ball_radius * ball_radius * ChVector<>(1, 1, 1);

    ChVector<> ball_pos(0, 0, 23);

    auto ball = std::make_shared<ChBody>(std::make_shared<ChCollisionModelDistributed>(), ChMaterialSurface::SMC);
    ball->SetMaterialSurface(ballMat);

    ball->SetMass(mass);
    ball->SetPos(ball_pos);
    ball->SetRot(ChQuaternion<>(1, 0, 0, 0));
    ball->SetBodyFixed(false);
    ball->SetCollide(true);

    ball->GetCollisionModel()->ClearModel();
    ChVector<> ball_hdim(2, 2, 2);

    utils::AddSphereGeometry(ball.get(), ball_radius);

    ball->GetCollisionModel()->BuildModel();

    my_sys->AddBody(ball);
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    int num_threads = 1;
    if (argc > 1) {
        num_threads = atoi(argv[1]);
    }

    omp_set_num_threads(num_threads);

    int thread_count = 0;
#pragma omp parallel reduction(+ : thread_count)
    { thread_count++; }

    std::cout << "Running on " << num_ranks << " MPI ranks.\n";
    std::cout << "Running on " << thread_count << " OpenMP threads.\n";

    double time_step = 1e-3;
    double time_end = 24;

    double out_fps = 50;

    unsigned int max_iteration = 100;
    double tolerance = 1e-3;

    print("Constructing the system...\n");
    ChSystemDistributed my_sys(MPI_COMM_WORLD, 1.0, 100000, std::string("../out") + std::to_string(my_rank) + ".txt");

    std::cout << "Node " << my_sys.node_name << "\n";

    my_sys.SetParallelThreadNumber(num_threads);
    CHOMPfunctions::SetNumThreads(num_threads);

    my_sys.Set_G_acc(ChVector<double>(0.01, 0.01, -9.8));

    // Set solver parameters
    my_sys.GetSettings()->solver.max_iteration_bilateral = max_iteration;
    my_sys.GetSettings()->solver.tolerance = tolerance;

    my_sys.GetSettings()->collision.narrowphase_algorithm = NarrowPhaseType::NARROWPHASE_R;
    my_sys.GetSettings()->collision.bins_per_axis = vec3(10, 10, 10);

    my_sys.GetSettings()->solver.contact_force_model = ChSystemSMC::ContactForceModel::Hertz;
    my_sys.GetSettings()->solver.adhesion_force_model = ChSystemSMC::AdhesionForceModel::Constant;

    ChVector<double> domlo(-5, -5, -1);
    ChVector<double> domhi(5, 5, 25);
    my_sys.GetDomain()->SetSplitAxis(0);
    my_sys.GetDomain()->SetSimDomain(domlo.x(), domhi.x(), domlo.y(), domhi.y(), domlo.z(), domhi.z());
    my_sys.GetDomain()->PrintDomain();

    AddContainer(&my_sys);
    AddFallingBalls(&my_sys);

    // Run simulation for specified time
    int num_steps = std::ceil(time_end / time_step);
    int out_steps = std::ceil((1 / time_step) / out_fps);
    int out_frame = 0;
    double time = 0;

    int checkpoints[4];
    checkpoints[0] = std::ceil(2 / time_step);
    checkpoints[1] = std::ceil(8 / time_step);
    checkpoints[2] = std::ceil(12 / time_step);
    checkpoints[3] = std::ceil(16 / time_step);

    // Run initial settling 2 sec
    for (int i = 0; i < num_steps; i++) {
        if (i % out_steps == 0) {
            OutputData(&my_sys, out_frame, time);
            out_frame++;
            real3 min = my_sys.data_manager->measures.collision.rigid_min_bounding_point;
            real3 max = my_sys.data_manager->measures.collision.rigid_max_bounding_point;
            std::cout << "Min: " << min[0] << " " << min[1] << " " << min[2] << " Max: " << max[0] << " " << max[1]
                      << " " << max[2] << "\n";
        }

        // OutputData(&my_sys, i, time);
        // my_sys.PrintShapeData();

        if (i == checkpoints[0]) {
            std::cout << "Resetting gravity: (-5, 0, -10)\n";
            my_sys.Set_G_acc(ChVector<>(-5, 0, -10));
            //    AddFallingBalls(&my_sys);
        }

        if (i == checkpoints[1]) {
            std::cout << "Resetting gravity: (0, 5, -10)\n";
            my_sys.Set_G_acc(ChVector<>(0, 5, -10));
        }
        if (i == checkpoints[2]) {
            std::cout << "Resetting gravity: (5, 0, -10)\n";
            my_sys.Set_G_acc(ChVector<>(5, 0, -10));
        }

        if (i == checkpoints[3]) {
            std::cout << "Resetting gravity: (0, -5, -10)\n";
            my_sys.Set_G_acc(ChVector<>(0, -5, -10));
        }

        Monitor(&my_sys);
        my_sys.DoStepDynamics(time_step);
        time += time_step;
    }

    MPI_Finalize();

    return 0;
}