#ifdef __MPI
#include "module_cell/parallel_kpoints.h"

#include <mpi.h>

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "module_base/global_variable.h"
#include "module_base/parallel_global.h"

/************************************************
 *  unit test of functions in parallel_kpoints.cpp
 ***********************************************/

/**
 * The tested functions:
 *   i. Parallel_Global::init_pools() is the public interface
 *      to call the private function Parallel_Global::divide_pools(),
 *      which divide all processes into KPAR groups.
 *   ii.Parallel_Kpoints::kinf() is the public interface
 *      to call another three functions: get_nks_pool(),
 *      get_startk_pool(), get_whichpool(), which divide all kpoints
 *      into KPAR groups.
 *   iii.Parallel_Kpoints::gatherkvec() is an interface to gather kpoints
 *      vectors from all processors.
 * The default number of processes is set to 4 in parallel_kpoints_test.sh.
 * One may modify it to do more tests, or adapt this unittest to local
 * environment.
 */

class ParaPrepare
{
public:
	ParaPrepare(int KPAR_in,int nkstot_in):
		KPAR_(KPAR_in),nkstot_(nkstot_in){}
	int KPAR_;
	int nkstot_;
	void test_init_pools(void);
	void test_kinfo(const Parallel_Kpoints* Pkpts);
};

void ParaPrepare::test_kinfo(const Parallel_Kpoints* Pkpts)
{
	std::vector<int> nks_pool_(KPAR_,0);
    std::vector<int> startk_pool_(KPAR_, 0);
    std::vector<int> whichpool_(nkstot_, 0);

    int quotient = nkstot_/KPAR_;
	int residue  = nkstot_%KPAR_;
	// the previous "residue" pools have (quotient+1) kpoints
	for(int i=0;i<KPAR_;i++)
	{
		nks_pool_[i] = quotient;
		if(i<residue)
		{
		        nks_pool_[i]++;
		}
		// number of kpoints in each pool
		EXPECT_EQ(Pkpts->nks_pool[i],nks_pool_[i]);
		//
		if(i>0)
		{
		  startk_pool_[i]=startk_pool_[i-1]+nks_pool_[i-1];
		}
		// the rank of the 1st process of each pool in MPI_COMM_WORLD
		EXPECT_EQ(Pkpts->startk_pool[i],startk_pool_[i]);
		//
		for(int ik=0;ik<nks_pool_[i];ik++)
		{
			int k_now = ik + startk_pool_[i];
			// the pool where this kpoint (k_now) resides
			EXPECT_EQ(Pkpts->whichpool[k_now],i);
		}
	}
}


void ParaPrepare::test_init_pools()
{
	int* nproc_pool_= new int[KPAR_];
	int quotient = GlobalV::NPROC/KPAR_;
	int residue  = GlobalV::NPROC%KPAR_;
	// the previous "residue" pools have (quotient+1) processes
	for(int i=0;i<KPAR_;i++)
	{
		nproc_pool_[i] = quotient;
		if(i<residue)
		{
			++nproc_pool_[i];
		}
	}
	int color=-1;
	int np_now=0;
	for(int i=0;i<KPAR_;i++)
	{
		np_now += nproc_pool_[i];
		if(GlobalV::MY_RANK < np_now)
		{
			color = i;
			// GlobalV::MY_POOL is the pool where this process resides
			EXPECT_EQ(GlobalV::MY_POOL,i);
			break;
		}
	}
	MPI_Comm test_comm;
	int test_rank, test_size;
	MPI_Comm_split(MPI_COMM_WORLD, color, GlobalV::MY_RANK, &test_comm);
	MPI_Comm_rank(test_comm, &test_rank);
	MPI_Comm_size(test_comm, &test_size);
	// GlobalV::RANK_IN_POOL is the rank of this process in GlobalV::MY_POOL
	EXPECT_EQ(GlobalV::RANK_IN_POOL,test_rank);
	// GlobalV::NPROC_IN_POOL is the number of processes in GlobalV::MY_POOL where this process resides
	EXPECT_EQ(GlobalV::NPROC_IN_POOL,test_size);
        //printf("my_rank: %d \t test rank/size: %d/%d \t pool rank/size: %d/%d\n",
	//       GlobalV::MY_RANK,test_rank,test_size,GlobalV::RANK_IN_POOL,GlobalV::NPROC_IN_POOL);
	MPI_Comm_free(&test_comm);
}

class ParaKpoints : public ::testing::TestWithParam<ParaPrepare>
{
};

TEST(Parallel_KpointsTest, GatherkvecTest) {
    // Initialize Parallel_Kpoints object
    Parallel_Kpoints parallel_kpoints;

    // Initialize local and global vectors
    std::vector<ModuleBase::Vector3<double>> vec_local;
    std::vector<ModuleBase::Vector3<double>> vec_global;

    // Populate vec_local with some data
	int npool = 1;
	if(GlobalV::NPROC > 2)
	{
		npool = 3;
	}
	else if(GlobalV::NPROC == 2)
	{
		npool = 2;
	}
	GlobalV::KPAR = npool;

	if(GlobalV::MY_RANK == 0)
	{
    	vec_local.push_back(ModuleBase::Vector3<double>(1.0, 1.0, 1.0));
		GlobalV::NPROC_IN_POOL = 1;
		GlobalV::MY_POOL = 0;
		parallel_kpoints.nks_np = 1;
	}
	else if(GlobalV::MY_RANK == 1)
	{
		vec_local.push_back(ModuleBase::Vector3<double>(2.0, 2.0, 2.0));
		vec_local.push_back(ModuleBase::Vector3<double>(3.0, 4.0, 5.0));
		GlobalV::NPROC_IN_POOL = 1;
		GlobalV::MY_POOL = 1;
		parallel_kpoints.nks_np = 2;
	}
	else
	{
		vec_local.push_back(ModuleBase::Vector3<double>(3.0, 3.0, 3.0));
		GlobalV::NPROC_IN_POOL = GlobalV::NPROC - 2;
		GlobalV::MY_POOL = 2;
		parallel_kpoints.nks_np = 1;
	}

	parallel_kpoints.startk_pool.resize(npool);
	parallel_kpoints.nkstot_np = 1;
	parallel_kpoints.startk_pool[0] = 0;
	if (npool >= 2)
	{
		parallel_kpoints.nkstot_np += 2;
		parallel_kpoints.startk_pool[1] = 1;
	}
	if(npool >= 3)
	{
		parallel_kpoints.nkstot_np += 1;
		parallel_kpoints.startk_pool[2] = 3;
	}

    // Call gatherkvec method
    parallel_kpoints.gatherkvec(vec_local, vec_global);

    // Check the values of vec_global
    EXPECT_EQ(vec_global[0].x, 1.0);
	EXPECT_EQ(vec_global[0].y, 1.0);
	EXPECT_EQ(vec_global[0].z, 1.0);

	if(npool >= 2)
	{
		EXPECT_EQ(vec_global[1].x, 2.0);
		EXPECT_EQ(vec_global[1].y, 2.0);
		EXPECT_EQ(vec_global[1].z, 2.0);
		EXPECT_EQ(vec_global[2].x, 3.0);
		EXPECT_EQ(vec_global[2].y, 4.0);
		EXPECT_EQ(vec_global[2].z, 5.0);
	}
	if(npool >= 3)
	{
		EXPECT_EQ(vec_global[3].x, 3.0);
		EXPECT_EQ(vec_global[3].y, 3.0);
		EXPECT_EQ(vec_global[3].z, 3.0);
	}
}

TEST_P(ParaKpoints,DividePools)
{
	ParaPrepare pp = GetParam();
	Parallel_Kpoints* Pkpoints;
	Pkpoints = new Parallel_Kpoints;
	GlobalV::KPAR = pp.KPAR_;
	if(pp.KPAR_>GlobalV::NPROC)
	{
		std::string output;
		testing::internal::CaptureStdout();
		EXPECT_EXIT(Parallel_Global::init_pools(),testing::ExitedWithCode(1),"");
		output = testing::internal::GetCapturedStdout();
		EXPECT_THAT(output,testing::HasSubstr("Too many pools"));
	}
	else
	{
		Parallel_Global::init_pools();
		pp.test_init_pools();
		Pkpoints->kinfo(pp.nkstot_);
		pp.test_kinfo(Pkpoints);
	}
	delete Pkpoints;
}

INSTANTIATE_TEST_SUITE_P(TESTPK,ParaKpoints,::testing::Values(
			// KPAR, nkstot
			ParaPrepare(2,57),
			ParaPrepare(3,67),
			ParaPrepare(5,97),
			ParaPrepare(97,97)
			));

int main(int argc, char **argv)
{

    MPI_Init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);

    MPI_Comm_size(MPI_COMM_WORLD,&GlobalV::NPROC);
    MPI_Comm_rank(MPI_COMM_WORLD,&GlobalV::MY_RANK);

    int result = RUN_ALL_TESTS();

    MPI_Finalize();

    return result;
}
#endif
