#include "Common.h"
#include "Driver.h"
//#include "PRAStar.h"
//#include "SearchUnit.h"
#include "UnitSimulation.h"
//#include "Directional2DEnvironment.h"
//#include "RandomUnit.h"
//#include "AStar.h"
//#include "GenericSearchUnit.h"
//#include "GenericPatrolUnit.h"
//#include "TemplateAStar.h"
//#include "MapSectorAbstraction.h"
//#include "DirectionalPlanner.h"
#include "ScenarioLoader.h"
#include "AirplaneSimple.h"
#include "AirplaneConstrained.h"
#include "AirplaneCBSUnits.h"

bool mouseTracking;
int px1, py1, px2, py2;
int absType = 0;
int mapSize = 128;
bool recording = false;
double simTime = 0;
double stepsPerFrame = 1.0/100.0;
double frameIncrement = 1.0/10000.0;
std::vector<airtimeState> thePath;

bool paused = false;

//DirectionalPlanner *dp = 0;
//MapSectorAbstraction *quad = 0;
//std::vector<DirectionSimulation *> unitSims;
//typedef GenericSearchUnit<xySpeedHeading, deltaSpeedHeading, Directional2DEnvironment> DirSearchUnit;
//typedef RandomUnit<xySpeedHeading, deltaSpeedHeading, Directional2DEnvironment> RandomDirUnit;
//typedef GenericPatrolUnit<xySpeedHeading, deltaSpeedHeading, Directional2DEnvironment> DirPatrolUnit;

//void RunBatchTest(char *file, model mm, heuristicType heuristic, int length);

/*
AirplaneEnvironment ae;
airplaneState s;
airplaneState s2;
airplaneState s3;
airplaneState s4;
airplaneState tmp;
 */


int main(int argc, char* argv[])
{
	InstallHandlers();
	RunHOGGUI(argc, argv);
}


/**
 * This function is used to allocate the unit simulated that you want to run.
 * Any parameters or other experimental setup can be done at this time.
 */
void CreateSimulation(int id)
{
	SetNumPorts(id, 1);
	
//	unitSims.resize(id+1);
//	unitSims[id] = new DirectionSimulation(new Directional2DEnvironment(map, kVehicle));
//	unitSims[id]->SetStepType(kRealTime);
//	unitSims[id]->GetStats()->EnablePrintOutput(true);
//	unitSims[id]->GetStats()->AddIncludeFilter("gCost");
//	unitSims[id]->GetStats()->AddIncludeFilter("nodesExpanded");
//	dp = new DirectionalPlanner(quad);
}

/**
 * Allows you to install any keyboard handlers needed for program interaction.
 */
void InstallHandlers()
{
	InstallKeyboardHandler(MyDisplayHandler, "Toggle Abstraction", "Toggle display of the ith level of the abstraction", kAnyModifier, '0', '9');
	InstallKeyboardHandler(MyDisplayHandler, "Cycle Abs. Display", "Cycle which group abstraction is drawn", kAnyModifier, '\t');
	InstallKeyboardHandler(MyDisplayHandler, "Pause Simulation", "Pause simulation execution.", kNoModifier, 'p');
	InstallKeyboardHandler(MyDisplayHandler, "Speed Up Simulation", "Speed Up simulation execution.", kNoModifier, '=');
	InstallKeyboardHandler(MyDisplayHandler, "Slow Down Simulation", "Slow Down simulation execution.", kNoModifier, '-');
	InstallKeyboardHandler(MyDisplayHandler, "Step Simulation", "If the simulation is paused, step forward .1 sec.", kNoModifier, 'o');
	InstallKeyboardHandler(MyDisplayHandler, "Recird", "Toggle recording.", kNoModifier, 'r');
	InstallKeyboardHandler(MyDisplayHandler, "Step History", "If the simulation is paused, step forward .1 sec in history", kAnyModifier, '}');
	InstallKeyboardHandler(MyDisplayHandler, "Step History", "If the simulation is paused, step back .1 sec in history", kAnyModifier, '{');
	InstallKeyboardHandler(MyDisplayHandler, "Step Abs Type", "Increase abstraction type", kAnyModifier, ']');
	InstallKeyboardHandler(MyDisplayHandler, "Step Abs Type", "Decrease abstraction type", kAnyModifier, '[');

	InstallKeyboardHandler(MyPathfindingKeyHandler, "Mapbuilding Unit", "Deploy unit that paths to a target, building a map as it travels", kNoModifier, 'd');
	InstallKeyboardHandler(MyRandomUnitKeyHandler, "Add A* Unit", "Deploys a simple a* unit", kNoModifier, 'a');
	InstallKeyboardHandler(MyRandomUnitKeyHandler, "Add simple Unit", "Deploys a randomly moving unit", kShiftDown, 'a');
	//InstallKeyboardHandler(MyRandomUnitKeyHandler, "Add simple Unit", "Deploys a right-hand-rule unit", kControlDown, '1');

	InstallCommandLineHandler(MyCLHandler, "-heuristic", "-heuristic <octile, perimeter, extendedPerimeter>", "Selects how many steps of the abstract path will be refined.");
	InstallCommandLineHandler(MyCLHandler, "-planlen", "-planlen <int>", "Selects how many steps of the abstract path will be refined.");
	InstallCommandLineHandler(MyCLHandler, "-scenario", "-scenario filename", "Selects the scenario to be loaded.");
	InstallCommandLineHandler(MyCLHandler, "-model", "-model <human, tank, vehicle>", "Selects the motion model.");
	InstallCommandLineHandler(MyCLHandler, "-map", "-map filename", "Selects the default map to be loaded.");
	InstallCommandLineHandler(MyCLHandler, "-seed", "-seed integer", "Sets the randomized number generator to use specified key.");
	InstallCommandLineHandler(MyCLHandler, "-batch", "-batch numScenarios", "Runs a bunch of test scenarios.");
	InstallCommandLineHandler(MyCLHandler, "-size", "-batch integer", "If size is set, we create a square maze with the x and y dimensions specified.");
	
	InstallWindowHandler(MyWindowHandler);

	InstallMouseClickHandler(MyClickHandler);
}

void MyWindowHandler(unsigned long windowID, tWindowEventType eType)
{
	if (eType == kWindowDestroyed)
	{
		printf("Window %ld destroyed\n", windowID);
		RemoveFrameHandler(MyFrameHandler, windowID, 0);
	}
	else if (eType == kWindowCreated)
	{
		glClearColor(0.6, 0.8, 1.0, 1.0);
		printf("Window %ld created\n", windowID);
		InstallFrameHandler(MyFrameHandler, windowID, 0);
		InitSim();
		CreateSimulation(windowID);
	}
}

AirplaneConstrainedEnvironment *ace = 0;
AirplaneConstrainedEnvironment *aces = 0;
UnitSimulation<airtimeState, airplaneAction, AirplaneConstrainedEnvironment> *sim = 0;
AirCBSGroup* group = 0;
AirCBSUnit* u1 = 0;
AirCBSUnit* u2 = 0;
AirCBSUnit* u3 = 0;
AirCBSUnit* u4 = 0;
AirCBSUnit* u5 = 0;
AirCBSUnit* u6 = 0;
airtimeState s1, s2, s3, s4, s5, s6, g1, g2, g3, g4, g5, g6;


void InitSim(){
    AirplaneEnvironment* ae = new AirplaneEnvironment();
    ae->loadPerimeterDB();
    AirplaneSimpleEnvironment* ase = new AirplaneSimpleEnvironment();
    ase->loadPerimeterDB();
	ace = new AirplaneConstrainedEnvironment(ae);
	aces = new AirplaneConstrainedEnvironment(reinterpret_cast<AirplaneEnvironment*>(ase));



	sim = new UnitSimulation<airtimeState, airplaneAction, AirplaneConstrainedEnvironment>(ace);
	sim->SetStepType(kLockStep);
	group = new AirCBSGroup(ace,ace,4);
    // TODO: Have it use the simple environment and switch to the complex one
    //       after too many conflicts
	//group = new AirCBSGroup(ace,aces,4);
	
	sim->AddUnitGroup(group);

	s1.x = 36;
	s1.y = 40;
	s1.height = 14;
	s1.heading = 2;
	s1.speed = 5;
	s1.t = 0;

	g1.x = 42;
	g1.y = 40;
	g1.height = 14;
	g1.heading = 6;
	g1.speed = 5;
	g1.t = 0;


	u1 = new AirCBSUnit(s1, g1);
	u1->SetColor(1.0, 0.0, 0.0);
	//group->AddUnit(u1);
	//sim->AddUnit(u1);

	std::cout << "Set unit 1 goal from " << s1 << " to " << g1 << " rough heading: " << (unsigned)s1.headingTo(g1) << std::endl;


	s2.x = 8;
	s2.y = 2;
	s2.x = 46;
	s2.y = 40;
	s2.height = 26;
	s2.height = 14;
	s2.heading = 3;
	s2.heading = 6;
	s2.speed = 1;
	s2.t = 0;

	g2.x = 76;
	g2.y = 58;
	g2.x = 38;
	g2.y = 40;
	g2.height = 14;
	g2.heading = 5;
	g2.heading = 6;
	g2.speed = 1;
	g2.t = 0;

	u2 = new AirCBSUnit(s2, g2);
	u2->SetColor(0.0, 1.0, 0.0);
	//sim->AddUnit(u2);
	//group->AddUnit(u2);
	
	std::cout << "Set unit 2 goal from " << s2 << " to " << g2 << " rough heading: " << (unsigned)s2.headingTo(g2) << std::endl;

	s3.x = 40;
	s3.y = 46;
	s3.height = 14;
	s3.heading = 0;
	s3.speed = 1;
	s3.t = 0;

	g3.x = 40;
	g3.y = 38;
	g3.height = 14;
	g3.heading = 0;
	g3.speed = 1;
	g3.t = 0;

	u3 = new AirCBSUnit(s3, g3);
	u3->SetColor(0.0, 0.0, 1.0);
	//sim->AddUnit(u3);
	//group->AddUnit(u3);
	
	std::cout << "Set unit 3 goal from " << s3 << " to " << g3 << " rough heading: " << (unsigned)s3.headingTo(g3) << std::endl;

	s4.x = 40;
	s4.y = 34;
	s4.height = 14;
	s4.heading = 4;
	s4.speed = 1;
	s4.t = 0;

	g4.x = 40;
	g4.y = 42;
	g4.height = 14;
	g4.heading = 4;
	g4.speed = 1;
	g4.t = 0;

	u4 = new AirCBSUnit(s4, g4);
	u4->SetColor(1.0, 0.0, 1.0);
	
	std::cout << "Set unit 4 goal from " << s4 << " to " << g4 << " rough heading: " << (unsigned)s4.headingTo(g4) << std::endl;


	s5.x = 18;
	s5.y = 23;
	s5.height = 0;
	s5.heading = 0;
	s5.speed = 0;
	s5.landed = true;
	s5.t = 0;

	g5.x = 48;
	g5.y = 7;
	g5.height = 16;
	g5.heading = 2;
	g5.speed = 2;
	g5.t = 0;

	u5 = new AirCBSUnit(s5, g5);
	u5->SetColor(1.0, 0.0, 1.0);
	
	std::cout << "Set unit 5 goal from " << s5 << " to " << g5 << " rough heading: " << (unsigned)s5.headingTo(g5) << std::endl;


	s6.x = 55;
	s6.y = 17;
	s6.height = 23;
	s6.speed = 3;
	s6.heading = 0;
	s6.landed = false;
	s6.t = 0;

	g6.x = 18;
	g6.y = 23;
	g6.height = 0;
	g6.heading = 0;
	g6.speed = 0;
	g6.landed = true;
	g6.t = 0;

	u6 = new AirCBSUnit(s6, g6);
	u6->SetColor(1.0, 0.0, 1.0);
	
	std::cout << "Set unit 6 goal from " << s6 << " to " << g6 << " rough heading: " << (unsigned)s6.headingTo(g6) << std::endl;

	group->AddUnit(u1);
	group->AddUnit(u2);
	group->AddUnit(u3);
	group->AddUnit(u4);
	group->AddUnit(u5);
	//group->AddUnit(u6);

	sim->AddUnit(u1);
	sim->AddUnit(u2);
	sim->AddUnit(u3);
	sim->AddUnit(u4);
	sim->AddUnit(u5);
	//sim->AddUnit(u6);


}

//std::vector<airplaneAction> acts;
void MyFrameHandler(unsigned long windowID, unsigned int viewport, void *)
{

	
	if (ace){
        for(auto u : group->GetMembers()){
            glLineWidth(2.0);
            ace->GLDrawPath(((AirCBSUnit const*)u)->GetPath());
            glLineWidth(1.0);
        }
    }

	if (sim)
		sim->OpenGLDraw();
	
	if (!paused)
		sim->StepTime(stepsPerFrame);

	/*
	if (recording)
	{
		static int index = 0;
		char fname[255];
		sprintf(fname, "/Users/nathanst/anim-%d%d%d", index/100, (index/10)%10, index%10);
		SaveScreenshot(windowID, fname);
		printf("Saving '%s'\n", fname);
		index++;
	}*/
}

int MyCLHandler(char *argument[], int maxNumArgs)
{
//	static model motionModel = kVehicle;
//	static int length = 3;
//	static heuristicType hType = kExtendedPerimeterHeuristic;
//	
//	if (strcmp(argument[0], "-heuristic") == 0)
//	{
//		if (strcmp(argument[1], "octile") == 0)
//		{
//			hType = kOctileHeuristic;
//			printf("Heuristic: octile\n");
//		}
//		else if (strcmp(argument[1], "perimeter") == 0)
//		{
//			hType = kPerimeterHeuristic;
//			printf("Heuristic: perimeter\n");
//		}
//		else if (strcmp(argument[1], "extendedPerimeter") == 0)
//		{
//			hType = kExtendedPerimeterHeuristic;
//			printf("Heuristic: extended perimeter\n");
//		}
//			
//	}
//	if (strcmp( argument[0], "-planlen" ) == 0 )
//	{
//		length = atoi(argument[1]);
//		printf("Plan Length: %d\n", length);
//		return 2;
//	}
//	if (strcmp( argument[0], "-model" ) == 0 )
//	{
//		if (strcmp(argument[1], "tank") == 0)
//		{
//			motionModel = kTank;
//			printf("Motion Model: Tank\n");
//		}
//		else if (strcmp(argument[1], "vehicle") == 0)
//		{
//			motionModel = kVehicle;
//			printf("Motion Model: Vehicle\n");
//		}
//		else if (strcmp(argument[1], "human") == 0)
//		{
//			motionModel = kHumanoid;
//			printf("Motion Model: Human\n");
//		}
//		return 2;
//	}
//	if (strcmp( argument[0], "-map" ) == 0 )
//	{
//		if (maxNumArgs <= 1)
//			return 0;
//		strncpy(gDefaultMap, argument[1], 1024);
//		return 2;
//	}
//	else if (strcmp( argument[0], "-seed" ) == 0 )
//	{
//		if (maxNumArgs <= 1)
//			return 0;
//		srand(atoi(argument[1]));
//		return 2;
//	}
//	else if (strcmp( argument[0], "-size" ) == 0 )
//	{
//		if (maxNumArgs <= 1)
//			return 0;
//		mapSize = atoi(argument[1]);
//		assert( mapSize > 0 );
//		printf("mapSize = %d\n", mapSize);
//		return 2;
//	}
//	else if (strcmp(argument[0], "-scenario") == 0)
//	{
//		printf("Scenario: %s\n", argument[1]);
//		RunBatchTest(argument[1], motionModel, hType, length);
//		exit(0);
//	}
	return 2; //ignore typos
}


void MyDisplayHandler(unsigned long windowID, tKeyboardModifier mod, char key)
{
	airplaneState b;
	switch (key)
	{
		case 'r': recording = !recording; break;
		case '[': recording = true; break;
		case ']': recording = false; break;
		case '\t':
			if (mod != kShiftDown)
				SetActivePort(windowID, (GetActivePort(windowID)+1)%GetNumPorts(windowID));
			else
			{
				SetNumPorts(windowID, 1+(GetNumPorts(windowID)%MAXPORTS));
			}
			break;
		case '-': 
                        if(stepsPerFrame>0)stepsPerFrame-=frameIncrement;
                        break;
		case '=': 
                        stepsPerFrame+=frameIncrement;
                        break;
		case 'p': 
			paused = !paused;
			break;//unitSims[windowID]->SetPaused(!unitSims[windowID]->GetPaused()); break;
		case 'o':
//			if (unitSims[windowID]->GetPaused())
//			{
//				unitSims[windowID]->SetPaused(false);
//				unitSims[windowID]->StepTime(1.0/30.0);
//				unitSims[windowID]->SetPaused(true);
//			}
			break;
		case 'd':
			
			break;
		default:
			break;
	}
}

void MyRandomUnitKeyHandler(unsigned long windowID, tKeyboardModifier mod, char)
{
	
}

void MyPathfindingKeyHandler(unsigned long , tKeyboardModifier , char)
{
//	// attmpt to learn!
//	Map m(100, 100);
//	Directional2DEnvironment d(&m);
//	//Directional2DEnvironment(Map *m, model envType = kVehicle, heuristicType heuristic = kExtendedPerimeterHeuristic);
//	xySpeedHeading l1(50, 50), l2(50, 50);
//	__gnu_cxx::hash_map<uint64_t, xySpeedHeading, Hash64 > stateTable;
//	
//	std::vector<xySpeedHeading> path;
//	TemplateAStar<xySpeedHeading, deltaSpeedHeading, Directional2DEnvironment> alg;
//	alg.SetStopAfterGoal(false);
//	alg.InitializeSearch(&d, l1, l1, path);
//	for (int x = 0; x < 2000; x++)
//		alg.DoSingleSearchStep(path);
//	int count = alg.GetNumItems();
//	LinearRegression lr(37, 1, 1/37.0); // 10 x, 10 y, dist, heading offset [16]
//	std::vector<double> inputs;
//	std::vector<double> output(1);
//	for (unsigned int x = 0; x < count; x++)
//	{
//		// note that the start state is always at rest;
//		// we actually want the goal state at rest?
//		// or generate everything by backtracking through the parents of each state
//		const AStarOpenClosedData<xySpeedHeading> val = GetItem(x);
//		inputs[0] = sqrt((val.data.x-l1.x)*(val.data.x-l1.x)+(val.data.y-l1.)*(val.data.y-l1.y));
//		// fill in values
//		if (fabs(val.data.x-l1.x) >= 10)
//			inputs[10] = 1;
//		else inputs[1+fabs(val.data.x-l1.x)] = 1;
//		if (fabs(val.data.y-l1.y) >= 10)
//			inputs[20] = 1;
//		else inputs[11+fabs(val.data.y-l1.y)] = 1;
//		// this is wrong -- I need the possibility of flipping 15/1 is only 2 apart
//		intputs[30+((int)(fabs(l1.rotation-val.data.rotation)))%16] = 1;
//		output[0] = val.g;
//		lr.train(inputs, output);
//		// get data and learn to predict the g-cost
//		//val.data.
//		//val.g;
//	}
}

bool MyClickHandler(unsigned long windowID, int, int, point3d loc, tButtonType button, tMouseEventType mType)
{
	return false;
	mouseTracking = false;
	if (button == kRightButton)
	{
		switch (mType)
		{
			case kMouseDown:
				//unitSims[windowID]->GetEnvironment()->GetMap()->GetPointFromCoordinate(loc, px1, py1);
				//printf("Mouse down at (%d, %d)\n", px1, py1);
				break;
			case kMouseDrag:
				mouseTracking = true;
				//unitSims[windowID]->GetEnvironment()->GetMap()->GetPointFromCoordinate(loc, px2, py2);
				//printf("Mouse tracking at (%d, %d)\n", px2, py2);
				break;
			case kMouseUp:
			{
//				if ((px1 == -1) || (px2 == -1))
//					break;
//				xySpeedHeading l1, l2;
//				l1.x = px1;
//				l1.y = py1;
//				l2.x = px2;
//				l2.y = py2;
//				DirPatrolUnit *ru1 = new DirPatrolUnit(l1, dp);
//				ru1->SetNumPatrols(1);
//				ru1->AddPatrolLocation(l2);
//				ru1->AddPatrolLocation(l1);
//				ru1->SetSpeed(2);
//				unitSims[windowID]->AddUnit(ru1);
			}
			break;
		}
		return true;
	}
	return false;
}
