/*
 *  Created by Thayne Walker.
 *  Copyright (c) Thayne Walker 2018 All rights reserved.
 *
 * This file is part of HOG2.
 *
 * HOG2 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * HOG2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with HOG; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef __hog2_glut__AirplaneCBSUnits__
#define __hog2_glut__AirplaneCBSUnits__

#include <iostream>
#include <limits> 
#include <algorithm>
#include <map>

#include <queue>
#include <functional>
#include <vector>

#include <thread>
#include <mutex>

#include "Unit.h"
#include "UnitGroup.h"
#include "ConstrainedEnvironment.h"
#include "VelocityObstacle.h"
//#include "TemplateIntervalTree.h"
#include "GridStates.h"
#include "MultiAgentStructures.h"
#include "TemporalAStar.h"
#include "Heuristic.h"
#include "Timer.h"
#include "SAP.h"
#include "Pairwise.h"
#include <string.h>
#include <unordered_map>

#define NO_CONFLICT    0
#define NON_CARDINAL   1
#define LEFT_CARDINAL  2
#define RIGHT_CARDINAL 4
#define BOTH_CARDINAL  (LEFT_CARDINAL|RIGHT_CARDINAL)

unsigned collchecks(0);
float collisionTime(0);
float planTime(0);
float replanTime(0);
float bypassplanTime(0);
float maplanTime(0);

template<typename BB, typename action, typename conflicttable, class searchalgo>
class CBSUnit;

extern double agentRadius;

template <class state>
struct CompareLowGCost {
  bool operator()(const AStarOpenClosedData<state> &i1, const AStarOpenClosedData<state> &i2)const{
    if(fequal(i1.g+i1.h, i2.g+i2.h)){
      if(fequal(i1.g, i2.g)){
        return i1.data.t<i2.data.t; // Break ties by time
      }
      return fless(i1.g, i2.g);
    }
    return fgreater(i1.g+i1.h, i2.g+i2.h);
  }
};

// To copy pointers of an object into the destination array...
template<typename BB>
void makeAABBs(std::vector<typename BB::State> const& v,
    std::vector<BB>& d, unsigned agent)
{
    d.reserve(v.size()-1);
    auto first(v.cbegin());
    while (first+1 != v.end()) {
        d.emplace_back(&*first,&*first+1,agent);
        ++first;
    }
}

// Helper functions

// Merge path between waypoints
template <typename BB, typename action, typename conflicttable, class searchalgo>
void MergeLeg(std::vector<BB> const& path, std::vector<BB>& thePath, std::vector<int>& wpts, const unsigned s, unsigned g, double minTime){
  int insertPoint(wpts[s]); // Starting index of this leg
  unsigned origTime(thePath[wpts[g]].start.t); // Original ending time of leg
  unsigned deletes(wpts[g]-wpts[s]+1); // Number of path points in this leg.
  // Remove points from the original path (if they exist)
  if(thePath.empty()){
    assert(!"Path being merged into is empty");
  }
  if(path.empty()){
    assert(!"Path to merge is empty");
  }
  while(thePath.size()>wpts[g]+1 && thePath[wpts[g]].start.sameLoc(thePath[++wpts[g]].start)){deletes++;}
  unsigned newTime(path.rbegin()->start.t); // Save the track end time of the new leg

  // Insert new path in front of the insert point
  thePath.insert(thePath.begin()+insertPoint,path.begin(),path.end());
  insertPoint += path.size();

  //Erase the original subpath including the start node
  thePath.erase(thePath.begin()+insertPoint,thePath.begin()+insertPoint+deletes);

  // Update waypoint indices
  int legLenDiff(path.size()-deletes);
  if(legLenDiff){
    for(int i(g); i<wpts.size(); ++i){
      wpts[i]+=legLenDiff;
    }
  }

  if(thePath.size()-1!=wpts[g] && newTime!=origTime){
      // Increase times through the end of the track
      auto newEnd(thePath.begin()+insertPoint);
      while(newEnd++ != thePath.end()){
          newEnd->start.t+=(newTime-origTime);
      }
  }

  while(wpts[g]>wpts[s]+1 && thePath[wpts[g]].start.sameLoc(thePath[wpts[g]-1].start)){
    wpts[g]--;
  }
}

// Plan path between waypoints
template <typename BB, typename action, typename conflicttable, class searchalgo>
unsigned ReplanLeg(CBSUnit<BB,action,conflicttable,searchalgo>* c, searchalgo& astar, ConstrainedEnvironment<BB,action>* env, std::vector<BB>& thePath, std::vector<int>& wpts, unsigned s, unsigned g, double minTime, unsigned agent){
  if(thePath.empty()){
    searchalgo::OpenList::Compare::currentAgent=agent;
    return GetFullPath(c, astar, env, thePath, wpts, agent);
    //assert(false && "Expected a valid path for re-planning.");
  }
  int insertPoint(wpts[s]); // Starting index of this leg
  unsigned origTime(thePath[wpts[g]].start.t); // Original ending time of leg
  unsigned deletes(wpts[g]-wpts[s]+1); // Number of path points in this leg.
  // Remove points from the original path (if they exist)
  while(thePath.size()>wpts[g]+1 && thePath[wpts[g]].start.sameLoc(thePath[++wpts[g]].start)){deletes++;}
  typename BB::State start(c->GetWaypoint(s));
  typename BB::State goal(c->GetWaypoint(g));
  // Preserve proper start time
  start.t = thePath[wpts[s]].start.t;

  //std::cout << start << " to " << goal << "\n";

  // Perform search for the leg
  std::vector<BB> path;
  env->setGoal(goal);
  Timer tmr;
  tmr.StartTimer();
  astar.GetPath(env, start, goal, path, minTime);
  replanTime+=tmr.EndTimer();
  //std::cout << "Replan took: " << tmr.EndTimer() << std::endl;
  //std::cout << "New leg " << path.size() << "\n";
  //for(auto &p: path){std::cout << p << "\n";}
  if(path.empty()){
    thePath.resize(0);
    return astar.GetNodesExpanded(); //no solution found
  }
  unsigned newTime(path.rbegin()->start.t); // Save the track end time of the new leg

  // Insert new path in front of the insert point
  //std::cout << "SIZE " << thePath.size() << "\n";
  //std::cout << "Insert path of len " << path.size() << " before " << insertPoint << "\n";
  thePath.insert(thePath.begin()+insertPoint,path.begin(),path.end());
  //std::cout << "SIZE " << thePath.size() << "\n";
  insertPoint += path.size();

  //Erase the original subpath including the start node
  //std::cout << "Erase path from " << insertPoint << " to " << (insertPoint+deletes) << "\n";
  thePath.erase(thePath.begin()+insertPoint,thePath.begin()+insertPoint+deletes);
  //std::cout << "SIZE " << thePath.size() << "\n";

  // Update waypoint indices
  int legLenDiff(path.size()-deletes);
  if(legLenDiff){
    for(int i(g); i<wpts.size(); ++i){
      wpts[i]+=legLenDiff;
    }
  }

  if(thePath.size()-1!=wpts[g] && newTime!=origTime){
      // Increase times through the end of the track
      auto newEnd(thePath.begin()+insertPoint);
      while(newEnd++ != thePath.end()){
          newEnd->start.t+=(newTime-origTime);
      }
  }

  while(wpts[g]>wpts[s]+1 && thePath[wpts[g]].start.sameLoc(thePath[wpts[g]-1].start)){
    wpts[g]--;
  }
  //std::cout << "Replanned path\n";
  //for(auto &p: thePath){std::cout << p << "\n";}
  //std::cout << "exp replan " << astar.GetNodesExpanded() << "\n";
  return astar.GetNodesExpanded();
}

// Plan path between waypoints
template <typename BB, typename action, typename conflicttable, class searchalgo>
unsigned GetFullPath(CBSUnit<BB,action,conflicttable,searchalgo>* c, searchalgo& astar, ConstrainedEnvironment<BB,action>* env, std::vector<BB>& thePath, std::vector<int>& wpts, unsigned agent)
{
  unsigned expansions(0);
  // We should only call this function for initial empty paths.
  if(thePath.size()) assert(!"Tried to plan on top of an existing path!");
  //thePath.resize(0);
  wpts.resize(c->GetNumWaypoints());
  wpts[0]=0;

  // Perform search for all legs
  unsigned offset(0);
  searchalgo::OpenList::Compare::currentEnv=(ConstrainedEnvironment<BB,action>*)env;
  if(searchalgo::OpenList::Compare::useCAT){
    searchalgo::OpenList::Compare::openList=astar.GetOpenList();
    searchalgo::OpenList::Compare::currentAgent=agent;
  }
  for(int i(0); i<wpts.size()-1; ++i){
    std::vector<BB> path;
    typename BB::State start(thePath.size()?thePath.back().start:c->GetWaypoint(i));
    //start.landed=false;
    //start.t=0;
    typename BB::State goal(c->GetWaypoint(i+1));
    env->setGoal(goal);

    Timer tmr;
    tmr.StartTimer();
    astar.GetPath(env, start, goal, path);
    planTime+=tmr.EndTimer();
    //std::cout << start <<"-->"<<goal<<" took: " << tmr.EndTimer() << std::endl;

    expansions += astar.GetNodesExpanded();
    //std::cout << "exp full " << astar.GetNodesExpanded() << "\n";
    if(path.empty()){return expansions;} //no solution found

    // Append to the entire path, omitting the first node for subsequent legs
    thePath.insert(thePath.end(),path.begin()+offset,path.end());
    
    offset=1;
    int ix(thePath.size()-1);
    wpts[i+1]=ix;
    while(ix>0&&thePath[ix].start.sameLoc(thePath[ix].end)){
      wpts[i+1]=--ix;
    }
  }
  return expansions;
}

template<typename BB, typename action, typename conflicttable,class searchalgo>
class CBSUnit : public Unit<typename BB::State, action, ConstrainedEnvironment<BB,action>> {
public:
  CBSUnit(std::vector<typename BB::State> const &gs, float viz=0)
:start(0), goal(1), current(gs[0]), waypoints(gs), visibility(viz) {}
  const char *GetName() { return "CBSUnit"; }
  bool MakeMove(ConstrainedEnvironment<BB,action> *,
      OccupancyInterface<typename BB::State,action> *,
      SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *,
      typename BB::State& a);
  bool MakeMove(ConstrainedEnvironment<BB,action> *,
      OccupancyInterface<typename BB::State,action> *,
      SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *,
      action& a);
  void UpdateLocation(ConstrainedEnvironment<BB,action> *, typename BB::State &newLoc, bool success,
      SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *)
  { if (success) current = newLoc; else assert(!"CBS Unit: Movement failed"); }

  void GetLocation(typename BB::State &l) { l = current; }
  void OpenGLDraw(const ConstrainedEnvironment<BB,action> *, const SimulationInfo<typename BB::State,action, ConstrainedEnvironment<BB,action>> *) const;
  void GetGoal(typename BB::State &s) { s = waypoints[goal]; }
  void GetStart(typename BB::State &s) { s = waypoints[start]; }
  inline std::vector<typename BB::State> const & GetWaypoints()const{return waypoints;}
  inline typename BB::State GetWaypoint(size_t i)const{ return waypoints[std::min(i,waypoints.size()-1)]; }
  inline unsigned GetNumWaypoints()const{return waypoints.size();}
  void SetPath(std::vector<typename BB::State> &p);
  /*void PushFrontPath(std::vector<BB> &s)
  {
    std::vector<BB> newPath;
    for (BB x : s)
      newPath.push_back(x);
    for (BB y : myPath)
      newPath.push_back(y);
    myPath = newPath;
  }*/
  inline std::vector<typename BB::State> const& GetPath()const{return myPath;}
  void UpdateGoal(typename BB::State &start, typename BB::State &goal);
  void setUnitNumber(unsigned n){number=n;}
  unsigned getUnitNumber()const{return number;}
  float getVisibility()const{return visibility;}

private:
  unsigned start, goal;
  typename BB::State current;
  std::vector<typename BB::State> waypoints;
  std::vector<typename BB::State> myPath;
  unsigned number;
  float visibility;
};

struct MetaAgent {
  MetaAgent(){}
  MetaAgent(unsigned agent){units.push_back(agent);}
  //std::vector<Constraint<BB>> c;
  std::vector<unsigned> units;
  std::string hint; // Instructions for the meta-agent planner
};

template<typename BB>
struct Conflict {
  Conflict<BB>():c(nullptr),unit1(9999999),prevWpt(0){}
  Conflict<BB>(Conflict<BB>const& from):c(from.c.release()),unit1(from.unit1),prevWpt(from.prevWpt){}
  Conflict<BB>& operator=(Conflict<BB>const& from){c.reset(from.c.release());unit1=from.unit1;prevWpt=from.prevWpt;}
  mutable std::unique_ptr<Constraint<BB>> c; // constraint representing one agent in the meta-typename BB::State
  unsigned unit1;
  unsigned prevWpt;
};

template<typename BB, typename conflicttable>
struct CBSTreeNode {
	CBSTreeNode():agent(9999999),parent(0),satisfiable(true),cat(){}
	CBSTreeNode(CBSTreeNode<BB,conflicttable> const& from,unsigned a):wpts(from.wpts),paths(from.paths),con(from.con),parent(from.parent),satisfiable(from.satisfiable),cat(from.cat),agent(a){
          paths[a]=&path;
        }
        CBSTreeNode(CBSTreeNode<BB,conflicttable> const& node, Conflict<BB> const& c, unsigned p, bool s, unsigned a):wpts(node.wpts),agent(a),paths(node.paths),con(c),parent(p),satisfiable(s),cat(node.cat){
          paths[a]=&path;
        }
        CBSTreeNode& operator=(CBSTreeNode<BB,conflicttable> const& from){
          wpts=from.wpts;
          paths=from.paths;
          con=from.con;
          parent=from.parent;
          satisfiable=from.satisfiable;
          cat=from.cat;
        }
	std::vector< std::vector<int> > wpts;
        unsigned agent;
	std::vector<BB> path;
        std::vector<std::vector<BB>*> paths;
	static Solution<BB> basepaths;
	//Solution<aabb> paths;
	Conflict<BB> con;
	unsigned int parent;
	bool satisfiable;
        //IntervalTree cat; // Conflict avoidance table
        conflicttable cat; // Conflict avoidance table
        BroadPhase<BB>* broadphase;
};

template<typename BB, typename conflicttable, class searchalgo>
static std::ostream& operator <<(std::ostream & out, const CBSTreeNode<BB,conflicttable> &act)
{
	out << "(paths:"<<act.paths->size()<<", parent: "<<act.parent<< ", satisfiable: "<<act.satisfiable<<")";
	return out;
}

template<class T, class C, class Cmp>
struct ClearablePQ:public std::priority_queue<T,C,Cmp>{
  void clear(){
    //std::cout << "Clearing pq\n";
    //while(this->size()){std::cout<<this->size()<<"\n";this->pop();}
    this->c.resize(0);
  }
};

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
class CBSGroup : public UnitGroup<typename BB::State, action, ConstrainedEnvironment<BB,action>>
{
  public:
    CBSGroup(std::vector<std::vector<EnvironmentContainer<BB,action>>>&, bool v=false);
    bool MakeMove(Unit<typename BB::State,action,ConstrainedEnvironment<BB,action>> *u,
        ConstrainedEnvironment<BB,action> *e, 
        SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si,
        action& a);
    bool MakeMove(Unit<typename BB::State,action,ConstrainedEnvironment<BB,action>> *u,
        ConstrainedEnvironment<BB,action> *e, 
        SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si,
        typename BB::State& a);
    void UpdateLocation(Unit<typename BB::State,action,ConstrainedEnvironment<BB,action>> *u,
        ConstrainedEnvironment<BB,action> *e, 
        typename BB::State &loc, bool success,
        SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si);
    void AddUnit(Unit<typename BB::State, action, ConstrainedEnvironment<BB,action>> *u);
    unsigned GetMaxTime(int location, int agent);
    void StayAtGoal(int location);

    void OpenGLDraw(const ConstrainedEnvironment<BB,action> *, const SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *)  const;
    double getTime() {return time;}
    bool donePlanning() {return planFinished;}
    bool ExpandOneCBSNode();

    std::vector<CBSTreeNode<BB,conflicttable> > tree;
    void processSolution(double);
    searchalgo astar;
    unsigned mergeThreshold;
    //void InitializeCBSTree();
    
  private:    

    unsigned LoadConstraintsForNode(int location, int agent=-1);
    bool Bypass(int best, std::pair<unsigned,unsigned> const& numConflicts, Conflict<BB> const& c1, unsigned otherunit, unsigned minTime);
    void Replan(int location);
    unsigned HasConflict(std::vector<BB> const& a, std::vector<int> const& wa, std::vector<BB> const& b, std::vector<int> const& wb, int x, int y, Conflict<BB> &c1, Conflict<BB> &c2, std::pair<unsigned,unsigned>& conflict, bool update);
    std::pair<unsigned,unsigned> FindHiPriConflict(CBSTreeNode<BB,conflicttable>  const& location, Conflict<BB> &c1, Conflict<BB> &c2, bool update=true);
    unsigned FindFirstConflict(CBSTreeNode<BB,conflicttable>  const& location, Conflict<BB> &c1, Conflict<BB> &c2);

    bool planFinished;

    /* Code for dealing with multiple environments */
    std::vector<std::vector<EnvironmentContainer<BB,action>>> environments;
    std::vector<EnvironmentContainer<BB,action>*> currentEnvironment;

    void SetEnvironment(unsigned conflicts,unsigned agent);
    void ClearEnvironmentConstraints(unsigned metaagent);
    void AddEnvironmentConstraint(Constraint<BB>* c, unsigned metaagent);

    double time;

    std::unordered_map<unsigned, MetaAgent> unitToMetaAgentMap;
    std::vector<MetaAgent> activeMetaAgents;
    std::vector<std::vector<unsigned>> metaAgentConflictMatrix;
    bool CheckForMerge(std::pair<unsigned, unsigned> &toMerge);

    unsigned int bestNode;
    std::mutex bestNodeLock;

    struct OpenListNode {
      OpenListNode() : location(0), cost(0), nc(0) {}
      OpenListNode(uint loc, double c, uint16_t n) : location(loc), cost(c),nc(n) {}
      std::ostream& operator <<(std::ostream& out)const{
        out << "(loc: "<<location<<", nc: "<<nc<< ", cost: "<<cost<<")";
        return out;
      }

      uint location;
      double cost;	
      unsigned nc;
    };
    struct OpenListNodeCompare {
      bool operator() (const OpenListNode& left, const OpenListNode& right) {
        if(greedyCT)
          return (left.nc==right.nc)?(fgreater(left.cost,right.cost)):(left.nc>right.nc);
        else
          return fequal(left.cost,right.cost)?(left.nc > right.nc):(fgreater(left.cost,right.cost));
      }
    };

    ClearablePQ<CBSGroup::OpenListNode, std::vector<CBSGroup::OpenListNode>, CBSGroup::OpenListNodeCompare> openList;

    uint TOTAL_EXPANSIONS = 0;

    //std::vector<SearchEnvironment<BB>*> agentEnvs;
public:
    // Algorithm parameters
    bool verify;
    bool nobypass;
    bool ECBSheuristic; // For ECBS
    unsigned killex; // Kill after this many expansions
    bool keeprunning; // Whether to keep running after finding the answer (e.g. for ui)
    int seed;
    Timer* timer;
    int animate; // Add pauses for animation
    static bool greedyCT;
    bool verbose=false;
    bool quiet=false;
    bool disappearAtGoal=true;
    bool broadphase=false;
    bool usecrossconstraints=true;
};

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
bool CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::greedyCT=false;

template<typename BB, typename conflicttable>
Solution<BB> CBSTreeNode<BB,conflicttable>::basepaths=Solution<BB>();

/** AIR CBS UNIT DEFINITIONS */

template<typename BB, typename action,typename conflicttable, class searchalgo>
void CBSUnit<BB,action,conflicttable,searchalgo>::SetPath(std::vector<typename BB::State> &p)
{
  myPath = p;
  std::reverse(myPath.begin(), myPath.end());
}

template<typename BB, typename action,typename conflicttable, class searchalgo>
void CBSUnit<BB,action,conflicttable,searchalgo>::OpenGLDraw(const ConstrainedEnvironment<BB,action> *ae, 
    const SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si) const
{
  GLfloat r, g, b;
  this->GetColor(r, g, b);
  ae->SetColor(r, g, b);

  if (myPath.size() > 1) {
    // Interpolate between the two given the timestep
    typename BB::State start_t = myPath[myPath.size()-1];
    typename BB::State stop_t = myPath[myPath.size()-2];

    if(si->GetSimulationTime()*BB::State::TIME_RESOLUTION_D <= stop_t.t && si->GetSimulationTime()*BB::State::TIME_RESOLUTION_D >= start_t.t)
    {
      float perc = (stop_t.t - si->GetSimulationTime()*BB::State::TIME_RESOLUTION_D)/(stop_t.t - start_t.t);
      ae->OpenGLDraw(stop_t, start_t, perc);
      //Constraint<BB> c(stop_t, start_t);
      //glColor3f(1, 0, 0);
      //c.OpenGLDraw();
    } else {		
      //ae->OpenGLDraw(stop_t);
      //glColor3f(1, 0, 0);
      //Constraint<BB> c(stop_t);
      //c.OpenGLDraw();
    }
  } else {
    //if (current.landed)
      //return;
    //ae->OpenGLDraw(current);
    //Constraint<BB> c(current);
    //glColor3f(1, 0, 0);
    //c.OpenGLDraw(si->GetEnvironment()->GetMap());
  }
}

/*void CBSUnit<BB,action,conflicttable,searchalgo>::UpdateGoal(typename BB::State &s, typename BB::State &g)
  {
  start = s;
  goal = g;
  }*/

//----------------------------------------------------------------------------------------------------------------------------------------------//

/** CBS GROUP DEFINITIONS */

//template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
//void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::InitializeCBSTree(){
  //CBSTreeNode<BB,conflicttable>::basepaths.resize(tree[0].paths.size());
//}

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::ClearEnvironmentConstraints(unsigned metaagent){
  for(unsigned agent : activeMetaAgents[metaagent].units){
    for (EnvironmentContainer<BB,action> env : this->environments[agent]) {
      env.environment->ClearConstraints();
    }
  }
}


template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::AddEnvironmentConstraint(Constraint<BB>* c, unsigned metaagent){
  //if(verbose)std::cout << "Add constraint " << c.start << "-->" << c.end << "\n";
  for(unsigned agent : activeMetaAgents[metaagent].units){
    for (EnvironmentContainer<BB,action> env : this->environments[agent]) {
      env.environment->AddConstraint(c);
    }
  }
}

/** constructor **/
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::CBSGroup(std::vector<std::vector<EnvironmentContainer<BB,action>>>& environvec, bool v)
: time(0), bestNode(0), planFinished(false), verify(false), nobypass(false)
    , ECBSheuristic(false), killex(INT_MAX), keeprunning(false),animate(0),
    seed(1234567), timer(0), verbose(v), mergeThreshold(5), quiet(true)
{
  //std::cout << "THRESHOLD " << threshold << "\n";

  tree.resize(1);
  tree[0].parent = 0;

  // Sort the environment container by the number of conflicts
  unsigned agent(0);
  for(auto& environs: environvec){
    std::sort(environs.begin(), environs.end(), 
        [](const EnvironmentContainer<BB,action>& a, const EnvironmentContainer<BB,action>& b) -> bool 
        {
        return a.threshold < b.threshold;
        }
        );
    for(auto& environ:environs){
      if(broadphase)
        environ.environment->constraints=new SAP<Constraint<BB>>(environvec.size());
      else
        environ.environment->constraints=new Pairwise<Constraint<BB>>(environvec.size());
    }
    environments.push_back(environs);
    // Set the current environment to that with 0 conflicts
    SetEnvironment(0,agent);
    ++agent;
  }

  CBSTreeNode<BB,conflicttable>::basepaths.resize(environments.size());
  //astar.SetVerbose(verbose);
}


/** Expand a single CBS node */
// Return true while processing
template<typename BB, typename action, typename conflicttable, class maplanner, class searchalgo>
bool CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::ExpandOneCBSNode()
{
  openList.pop();
  // There's no reason to expand if the plan is finished.
  if (planFinished)
    return false;

  Conflict<BB> c1, c2;
  unsigned long last = tree.size();

  auto numConflicts(FindHiPriConflict(tree[bestNode], c1, c2));
  // If no conflicts are found in this node, then the path is done
  if (numConflicts.first==0)
  {
    processSolution(timer->EndTimer());
  }else{
    // If the conflict is NON_CARDINAL, try the bypass
    // if semi-cardinal, try bypass on one and create a child from the other
    // if both children are cardinal, create children for both

    // Swap units
    //unsigned tmp(c1.unit1);
    //c1.unit1=c2.unit1;
    //c2.unit1=tmp;
    // Notify the user of the conflict
      if(!quiet)std::cout << "TREE " << bestNode <<"("<<tree[bestNode].parent << ") " <<(numConflicts.second==7?"CARDINAL":(numConflicts.second==3?"LEFT-CARDINAL":(numConflicts.second==5?"RIGHT-CARDINAL":"NON-CARDINAL")))<< " conflict found between MA " << c1.unit1 << " and MA " << c2.unit1 << " @:" << c2.c->start << "-->" << c2.c->end <<  " and " << c1.c->start << "-->" << c1.c->end << " NC " << numConflicts.first << " prev-W " << c1.prevWpt << " " << c2.prevWpt << "\n";
    //if(verbose){
      //std::cout << c1.unit1 << ":\n";
      //for(auto const& a:tree[bestNode].paths[c1.unit1]){
        //std::cout << a << "\n";
      //}
      //std::cout << c2.unit1 << ":\n";
      //for(auto const& a:tree[bestNode].paths[c2.unit1]){
        //std::cout << a << "\n";
      //}
    //}
    if(animate){
      c1.c->OpenGLDraw(currentEnvironment[0]->environment->GetMap());
      c2.c->OpenGLDraw(currentEnvironment[0]->environment->GetMap());
      usleep(animate*1000);
    }

    for(unsigned i(0); i < activeMetaAgents.size(); ++i){
      for(unsigned j(i+1); j < activeMetaAgents.size(); ++j){
        if(metaAgentConflictMatrix[i][j] > mergeThreshold){
          if(!quiet)std::cout << "Merging " << i << " and " << j << "\n";    
          // Merge i and j
          for(unsigned x : activeMetaAgents[j].units){
            activeMetaAgents[i].units.push_back(x);
          }
          // Remove j from the active list
          activeMetaAgents.erase(activeMetaAgents.begin() + j);
          // Remove j from the conflict matrix
          for(int x(0); x < metaAgentConflictMatrix.size(); ++x){
            metaAgentConflictMatrix[x].erase(metaAgentConflictMatrix[x].begin() + j);
          }
          metaAgentConflictMatrix.erase(metaAgentConflictMatrix.begin() + j);
          // Reset the hint
          activeMetaAgents[i].hint="";

          // Reset the search (This is the merge and restart enhancement)
          // Clear up the rest of the tree and clean the open list
          tree.resize(1);
          bestNode=0;
          openList.clear();
          //openList=ClearablePQ<CBSGroup::OpenListNode, std::vector<CBSGroup::OpenListNode>, CBSGroup::OpenListNodeCompare>();
          openList.emplace(0, 0, 0);
          // Clear all constraints from environments
          for(auto const& e:currentEnvironment){
            e->environment->ClearConstraints();
          }

          // Re-Plan the merged meta-agent
          ClearEnvironmentConstraints(i);
          // Build the MultiAgentState
          MultiAgentState<typename BB::State> start(activeMetaAgents[i].units.size());
          MultiAgentState<typename BB::State> goal(activeMetaAgents[i].units.size());
          std::vector<EnvironmentContainer<BB,action>*> envs(activeMetaAgents[i].units.size());
          if(verbose)std::cout << "Re-planning MA "<<i<<" consisting of agents:";
          for (unsigned x(0); x < activeMetaAgents[i].units.size(); x++) {
            if(verbose)std::cout<<" "<<activeMetaAgents[i].units[x];
            // Select the air unit from the group
            CBSUnit<BB,action,conflicttable,searchalgo> *c((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(activeMetaAgents[i].units[x]));
            // Retreive the unit start and goal
            typename BB::State s,g;
            c->GetStart(s);
            c->GetGoal(g);
            start[x]=s;
            goal[x]=g;
            envs[x]=currentEnvironment[activeMetaAgents[i].units[x]];
          }

          if(verbose)std::cout<<"\n";

          Solution<BB> solution;

          maplanner maPlanner;
          //maPlanner.SetVerbose(verbose);
          maPlanner.quiet=quiet;
          maPlanner.suboptimal=greedyCT;
          Timer tmr;
          tmr.StartTimer();
          maPlanner.GetSolution(envs, start, goal, solution, activeMetaAgents[i].hint);
          maplanTime+=tmr.EndTimer();
          if(!quiet)std::cout << "Merged plan took " << maPlanner.GetNodesExpanded() << " expansions\n";

          TOTAL_EXPANSIONS += maPlanner.GetNodesExpanded();

          if(verbose){
            std::cout << "Before merge:\n";
            for (unsigned int x = 0; x < tree[0].paths.size(); x++){
              if(tree[0].paths[x]->size()){
                std::cout << "Agent " << x << ": " << "\n";
                unsigned wpt(0);
                signed ix(0);
                for(auto &a:*tree[0].paths[x])
                {
                  //std::cout << a << " " << wpt << " " << unit->GetWaypoint(wpt) << "\n";
                  if(ix++==tree[0].wpts[x][wpt])
                  {
                    std::cout << " *" << a << "\n";
                    if(wpt<tree[0].wpts[x].size()-1)wpt++;
                  }
                  else
                  {
                    std::cout << "  " << a << "\n";
                  }
                }
              }else{
                std::cout << "Agent " << x << ": " << "NO Path Found.\n";
              }
            }
          }
          for (unsigned k(0); k < activeMetaAgents[i].units.size(); k++) {
            unsigned theUnit(activeMetaAgents[i].units[k]);
            unsigned minTime(GetMaxTime(0,theUnit)-1.0); // Take off a 1-second wait action, otherwise paths will grow over and over.
            MergeLeg<BB,action,conflicttable,searchalgo>(solution[k],*tree[0].paths[theUnit],tree[0].wpts[theUnit],0,1,minTime);
            //CBSUnit<BB,action,conflicttable,searchalgo> *c((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(theUnit));
            // Add the path back to the tree (new constraint included)
            //tree[0].paths[theUnit].resize(0);
            //unsigned wpt(0);
            //for (unsigned l(0); l < solution[k].size(); l++)
            //{
            //tree[0].paths[theUnit].push_back(solution[k][l]);
            //if(solution[k][l].sameLoc(c->GetWaypoint(tree[0].wpts[theUnit][wpt]))&&wpt<tree[0].wpts[theUnit].size()-1)wpt++;
            //else tree[0].wpts[theUnit][wpt]=l;
            //}
          }
          if(verbose){
            std::cout << "After merge:\n";
            for (unsigned int x = 0; x < tree[0].paths.size(); x++){
              if(tree[0].paths[x]->size()){
                std::cout << "Agent " << x << ": " << "\n";
                unsigned wpt(0);
                signed ix(0);
                for(auto &a:*tree[0].paths[x])
                {
                  //std::cout << a << " " << wpt << " " << unit->GetWaypoint(wpt) << "\n";
                  if(ix++==tree[0].wpts[x][wpt])
                  {
                    std::cout << " *" << a << "\n";
                    if(wpt<tree[0].wpts[x].size()-1)wpt++;
                  }
                  else
                  {
                    std::cout << "  " << a << "\n";
                  }
                }
              }else{
                std::cout << "Agent " << x << ": " << "NO Path Found.\n";
              }
            }
          }

          // Get the best node from the top of the open list, and remove it from the list
          bestNode = openList.top().location;

          // Set the visible paths for every unit in the node
          for (unsigned int x = 0; x < tree[bestNode].paths.size(); x++)
          {
            // Grab the unit
            CBSUnit<BB,action,conflicttable,searchalgo>* unit((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(x));

            // Prune these paths to the current simulation time
            typename BB::State current;
            unit->GetLocation(current);
            std::vector<typename BB::State> newPath;
            newPath.push_back(current); // Add the current simulation node to the new path

            // For everything in the path that's new, add the path back
            for (auto const& xNode :*tree[bestNode].paths[x]) {
              if (current.t < xNode.start.t) {
                newPath.push_back(xNode.start);
              }
            }

            // Update the actual unit path
            unit->SetPath(newPath);
          }

          if(!quiet)std::cout << "Merged MAs " << i << " and " << j << std::endl;
          // Finished merging - return from the unit
          return true; 
        }
      }
    }
    unsigned minTime(0);
    // If this is the last waypoint, the plan needs to extend so that the agent sits at the final goal
    //if(bestNode==0 || (activeMetaAgents[c1.unit1].units.size()==1 && tree[bestNode].con.prevWpt+1==tree[bestNode].wpts[activeMetaAgents[c1.unit1].units[0]].size()-1)){
      minTime=GetMaxTime(bestNode,c1.unit1)-1.0; // Take off a 1-second wait action, otherwise paths will grow over and over.
    //}
    if((numConflicts.second&LEFT_CARDINAL) || !Bypass(bestNode,numConflicts,c1,c2.unit1,minTime)){
      last = tree.size();
      //tree.resize(last+1);
      tree.emplace_back(tree[bestNode],c1,bestNode,true,c1.unit1);
      Replan(last);
      unsigned nc1(numConflicts.first);
      double cost = 0;
      for (int y = 0; y < tree[last].paths.size(); y++){
        if(verbose){
          std::cout << "Agent " << y <<":\n";
          for(auto const& ff:*tree[last].paths[y]){
            std::cout << ff << "\n";
          }
          std::cout << "cost: " << currentEnvironment[y]->environment->GetPathLength(*tree[last].paths[y]) << "\n";
        }
        cost += currentEnvironment[y]->environment->GetPathLength(*tree[last].paths[y]);
      }
      if(verbose){
        std::cout << "New CT NODE: " << last << " replanned: " << c1.unit1 << " cost: " << cost << " " << nc1 << "\n";
      }
      openList.emplace(last, cost, nc1);
    }
    //if(tree[bestNode].con.prevWpt+1==tree[bestNode].wpts[c2.unit1].size()-1){
      minTime=GetMaxTime(bestNode,c2.unit1)-1.0; // Take off a 1-second wait action, otherwise paths will grow over and over.
    //}
    if((numConflicts.second&RIGHT_CARDINAL) || !Bypass(bestNode,numConflicts,c2,c1.unit1,minTime)){
      last = tree.size();
      //tree.resize(last+1);
      tree.emplace_back(tree[bestNode],c2,bestNode,true,c2.unit1);
      Replan(last);
      unsigned nc1(numConflicts.first);
      double cost = 0;
      for (int y = 0; y < tree[last].paths.size(); y++){
        if(verbose){
          std::cout << "Agent " << y <<":\n";
          for(auto const& ff:*tree[last].paths[y]){
            std::cout << ff << "\n";
          }
          std::cout << "cost: " << currentEnvironment[y]->environment->GetPathLength(*tree[last].paths[y]) << "\n";
        }
        cost += currentEnvironment[y]->environment->GetPathLength(*tree[last].paths[y]);
      }
      if(verbose){
        std::cout << "New CT NODE: " << last << " replanned: " << c2.unit1 << " cost: " << cost << " " << nc1 << "\n";
      }
      openList.emplace(last, cost, nc1);
    }

    // Get the best node from the top of the open list, and remove it from the list
    int count(0);
    do{
    bestNode = openList.top().location;
    if(!tree[bestNode].satisfiable)openList.pop();
    if(++count > tree.size()) assert(!"No solution!?!");
    }while(!tree[bestNode].satisfiable);

    // Set the visible paths for every unit in the node
    if(keeprunning)
    for (unsigned int x = 0; x < tree[bestNode].paths.size(); x++)
    {
      // Grab the unit
      CBSUnit<BB,action,conflicttable,searchalgo> *unit = (CBSUnit<BB,action,conflicttable,searchalgo>*) this->GetMember(x);

      // Prune these paths to the current simulation time
      typename BB::State current;
      unit->GetLocation(current);
      std::vector<typename BB::State> newPath;
      newPath.push_back(current); // Add the current simulation node to the new path

      // For everything in the path that's new, add the path back
      for (BB const& xNode :*tree[bestNode].paths[x]) {
        if (current.t < xNode.start.t) {
          newPath.push_back(xNode.start);
        }
      }
      newPath.push_back(tree[bestNode].paths[x]->back().start);

      // Update the actual unit path
      unit->SetPath(newPath);
    }

  }
  return true;
}

template<typename BB, typename action, typename conflicttable, class searchalgo>
bool CBSUnit<BB,action,conflicttable,searchalgo>::MakeMove(ConstrainedEnvironment<BB,action> *ae,
    OccupancyInterface<typename BB::State,action> *,
SimulationInfo<typename BB::State,action,
    ConstrainedEnvironment<BB,action>> * si, typename BB::State& a)
{
  if (myPath.size() > 1 && si->GetSimulationTime()*BB::State::TIME_RESOLUTION_D > myPath[myPath.size()-2].t)
  {
    a=myPath[myPath.size()-2];

    //std::cout << "Moved from " << myPath[myPath.size()-1] << " to " << myPath[myPath.size()-2] << std::endl;
    //a = ae->GetAction(myPath[myPath.size()-1], myPath[myPath.size()-2]);
    //std::cout << "Used action " << a << "\n";
    myPath.pop_back();
    return true;
  }
  return false;
}

template<typename BB, typename action, typename conflicttable, class searchalgo>
bool CBSUnit<BB,action,conflicttable,searchalgo>::MakeMove(ConstrainedEnvironment<BB,action> *ae,
    OccupancyInterface<typename BB::State,action> *,
    SimulationInfo<typename BB::State,action,
    ConstrainedEnvironment<BB,action>> * si, action& a){
  if (myPath.size() > 1 && si->GetSimulationTime()*BB::State::TIME_RESOLUTION_D > myPath[myPath.size()-2].t)
  {

    //std::cout << "Moved from " << myPath[myPath.size()-1] << " to " << myPath[myPath.size()-2] << std::endl;
    //a = ae->GetAction(myPath[myPath.size()-1], myPath[myPath.size()-2]);
    //std::cout << "Used action " << a << "\n";
    myPath.pop_back();
    return true;
  }
  return false;
}

template<typename BB, typename action, typename conflicttable, class maplanner, class searchalgo>
bool CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::MakeMove(Unit<typename BB::State, action, ConstrainedEnvironment<BB,action>> *u, ConstrainedEnvironment<BB,action> *e,
    SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si, typename BB::State& a)
{
  if (planFinished && si->GetSimulationTime() > time)
  {
    return u->MakeMove(e,0,si,a);
  }
  else if ((si->GetSimulationTime() - time) < 0.0001)
  {
    return false;
  }
  else {
    time = si->GetSimulationTime();
    ExpandOneCBSNode();
  }
  return false;
}

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
bool CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::MakeMove(Unit<typename BB::State,action,ConstrainedEnvironment<BB,action>> *u, ConstrainedEnvironment<BB,action> *e,
    SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si, action& a){
  if (planFinished && si->GetSimulationTime() > time)
  {
    return u->MakeMove(e,0,si,a);
  }
  else if ((si->GetSimulationTime() - time) < 0.0001)
  {
    return false;
  }
  else {
    time = si->GetSimulationTime();
    ExpandOneCBSNode();
  }
  return false;
}

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::processSolution(double elapsed){
  double cost(0.0);
  unsigned total(0);
  unsigned maxTime(GetMaxTime(bestNode,9999999));
  // For every unit in the node
  bool valid(true);
  if(!quiet){
    for(int a(0); a<activeMetaAgents.size(); ++a){
      std::cout << "MA " << a <<": ";
      for(auto const& f:activeMetaAgents[a].units){
        std::cout << f << " ";
      }
      std::cout << "\n";
    }
  }
  for (unsigned int x = 0; x < tree[bestNode].paths.size(); x++){
    cost += currentEnvironment[x]->environment->GetPathLength(*tree[bestNode].paths[x]);
    total += tree[bestNode].paths[x]->size();

    // Grab the unit
    CBSUnit<BB,action,conflicttable,searchalgo>* unit((CBSUnit<BB,action,conflicttable,searchalgo>*) this->GetMember(x));

    // Prune these paths to the current simulation time
    /*typename BB::State current;
      unit->GetLocation(current);
      std::vector<typename BB::State> newPath;
      newPath.push_back(current); // Add the current simulation node to the new path

    // For everything in the path that's new, add the path back
    for (typename BB::State xNode : tree[bestNode].paths[x]) {
    if (current.t < xNode.t - 0.0001) {
    newPath.push_back(xNode);
    }
    }*/

    // Update the actual unit path
    // Add an extra wait action for "visualization" purposes,
    // This should not affect correctness...
    //if(tree[bestNode].paths[x]->size() && tree[bestNode].paths[x]->back().t<maxTime){
      //tree[bestNode].paths[x].push_back(tree[bestNode].paths[x].back());
      //tree[bestNode].paths[x].back().t=maxTime;
    //}

    // For everything in the path that's new, add the path back
    std::vector<typename BB::State> newPath;
    for (BB const& xNode :*tree[bestNode].paths[x]) {
      newPath.push_back(xNode.start);
    }
    newPath.push_back(tree[bestNode].paths[x]->back().start);
    unit->SetPath(newPath);
    if(tree[bestNode].paths[x]->size()){
      if(!quiet)std::cout << "Agent " << x << ": " << "\n";
      unsigned wpt(0);
      signed ix(0);
      for(auto &a:*tree[bestNode].paths[x])
      {
        //std::cout << a << " " << wpt << " " << unit->GetWaypoint(wpt) << "\n";
        if(ix++==tree[bestNode].wpts[x][wpt])
        {
          if(!quiet)std::cout << " *" << a << "\n";
          if(wpt<tree[bestNode].wpts[x].size()-1)wpt++;
        }
        else
        {
          if(!quiet)std::cout << "  " << a << "\n";
        }
      }
    }else{
      if(!quiet)std::cout << "Agent " << x << ": " << "NO Path Found.\n";
    }
    // Only verify the solution if the run didn't time out
    if(verify&&elapsed>0){
      for(unsigned int y = x+1; y < tree[bestNode].paths.size(); y++){
        auto a(tree[bestNode].paths[x]->begin());
        auto b(tree[bestNode].paths[y]->begin());
        while(a!=tree[bestNode].paths[x]->end() && b!=tree[bestNode].paths[y]->end()){
          if(collisionCheck3D(a->start,a->end,b->start,b->end,agentRadius)){
              valid=false;
              std::cout << "ERROR: Solution invalid; collision at: " << x <<":" << *a << ", " << y <<":" << *b << std::endl;
          }
          if(a->start.t<b->start.t){
            ++a;
          }else if(a->start.t>b->start.t){
            ++b;
          }else{
            ++a;++b;
          }
        }
      }
    }
  }
  fflush(stdout);
  std::cout<<"elapsed,planTime,replanTime,bypassplanTime,maplanTime,collisionTime,expansions,CATcollchecks,collchecks,collisions,cost,actions\n";
  if(verify&&elapsed>0)std::cout << (valid?"VALID":"INVALID")<<std::endl;
  if(elapsed<0){
    //std::cout << seed<<":FAILED\n";
    std::cout << seed<<":" << elapsed*(-1.0) << ",";
  }else{
    std::cout << seed<<":" << elapsed << ",";
  }
  std::cout << planTime << ",";
  std::cout << replanTime << ",";
  std::cout << bypassplanTime << ",";
  std::cout << maplanTime << ",";
  std::cout << collisionTime << ",";
  std::cout << TOTAL_EXPANSIONS << ",";
  std::cout << searchalgo::OpenList::Compare::collchecks << ",";
  std::cout << collchecks << ",";
  std::cout << tree.size() << ",";
  std::cout << cost/BB::State::TIME_RESOLUTION_D << ","; 
  std::cout << total << std::endl;
  TOTAL_EXPANSIONS = 0;
  planFinished = true;
  if(!keeprunning)exit(0);
}

/** Update the location of a unit */
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::UpdateLocation(Unit<typename BB::State, action, ConstrainedEnvironment<BB,action>> *u, ConstrainedEnvironment<BB,action> *e, typename BB::State &loc, 
    bool success, SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> *si)
{
  u->UpdateLocation(e, loc, success, si);
}

template<typename BB, typename action, typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::SetEnvironment(unsigned numConflicts, unsigned agent){
  bool set(false);
  if(currentEnvironment.size()<agent+1){
    currentEnvironment.resize(agent+1); // We make the assumption that agents are continuously numbered
  }
  for (int i = 0; i < this->environments[agent].size(); i++) {
    if (numConflicts >= environments[agent][i].threshold) {
      if(verbose)std::cout << "Setting to env# " << i << " b/c " << numConflicts << " >= " << environments[agent][i].threshold<<environments[agent][i].environment->name()<<std::endl;
      //std::cout<<environments[agent][i].environment->getGoal()<<"\n";
      currentEnvironment[agent] = &(environments[agent][i]);
      set=true;
    } else {
      break;
    }
  }
  if(!set)assert(false&&"No env was set - you need -cutoffs of zero...");

  astar.SetHeuristic(currentEnvironment[agent]->heuristic);
  astar.SetWeight(currentEnvironment[agent]->astar_weight);
}

/** Add a new unit with a new start and goal typename BB::State to the CBS group */
// Note: this should never be called with a meta agent of size>1
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::AddUnit(Unit<typename BB::State, action, ConstrainedEnvironment<BB,action>> *u)
{
  astar.SetExternalExpansionsPtr(&TOTAL_EXPANSIONS);
  astar.SetExternalExpansionLimit(killex);

  CBSUnit<BB,action,conflicttable,searchalgo> *c = (CBSUnit<BB,action,conflicttable,searchalgo>*)u;
  unsigned theUnit(this->GetNumMembers());
  c->setUnitNumber(theUnit);
  // Add the new unit to the group, and construct an CBSUnit
  UnitGroup<typename BB::State,action,ConstrainedEnvironment<BB,action>>::AddUnit(u);

  activeMetaAgents.push_back(MetaAgent(theUnit));
  unitToMetaAgentMap[theUnit] = activeMetaAgents.size()-1;

  // Add the new meta-agent to the conflict matrix
  metaAgentConflictMatrix.push_back(std::vector<unsigned>(activeMetaAgents.size()));
  for(unsigned i(0); i < activeMetaAgents.size(); ++i){
    metaAgentConflictMatrix[i].push_back(0);
    metaAgentConflictMatrix.back()[i] = 0;
    // Clear the constraints from the environment set
    ClearEnvironmentConstraints(i);
  }

  SetEnvironment(0,theUnit);

  // Setup the start and goal in the graph
  //c->GetStart(start);
  //c->GetGoal(goal);

  // Resize the number of paths in the root of the tree
  tree[0].paths.resize(this->GetNumMembers());
  tree[0].paths.back()=&CBSTreeNode<BB,conflicttable>::basepaths[this->GetNumMembers()-1];
  tree[0].wpts.resize(this->GetNumMembers());
  //agentEnvs.resize(this->GetNumMembers());

  // Recalculate the optimum path for the root of the tree
  //std::cout << "AddUnit "<<(theUnit) << " getting path." << std::endl;
  //std::cout << "Search using " << currentEnvironment[theUnit]->environment->name() << "\n";
  //agentEnvs[c->getUnitNumber()]=currentEnvironment[theUnit]->environment;
  searchalgo::OpenList::Compare::CAT = &(tree[0].cat);
  searchalgo::OpenList::Compare::CAT->set(&tree[0].paths);
  astar.SetAgent(theUnit);
  GetFullPath<BB,action,conflicttable,searchalgo>(c, astar, currentEnvironment[theUnit]->environment, *tree[0].paths.back(),tree[0].wpts.back(),theUnit);
  if(killex != INT_MAX && TOTAL_EXPANSIONS>killex)
      processSolution(-timer->EndTimer());
  //std::cout << "AddUnit agent: " << (theUnit) << " expansions: " << astar.GetNodesExpanded() << "\n";

  // Create new conflict avoidance table instance
  if(this->GetNumMembers()<2) tree[0].cat=conflicttable();
  // We add the optimal path to the root of the tree
  if(searchalgo::OpenList::Compare::useCAT){
    tree[0].cat.insert(*tree[0].paths.back(),currentEnvironment[theUnit]->environment,tree[0].paths.size()-1);
  }
  StayAtGoal(0); // Do this every time a unit is added because these updates are taken into consideration by the CAT

  // Set the plan finished to false, as there's new updates
  planFinished = false;

  // Clear up the rest of the tree and clean the open list
  tree.resize(1);
  bestNode=0;
  openList.clear();
  openList.emplace(0, 0, 0);
}

template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
unsigned CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::GetMaxTime(int location,int agent){

  unsigned maxDuration(0);
  if(disappearAtGoal)return 0;

  int i(0);
  // Find max duration of all paths
  for(auto const& n:tree[location].paths){
    if(agent!=i++)
      maxDuration=std::max(maxDuration,n->back().start.t);
  }
  return maxDuration;
}


template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::StayAtGoal(int location){

  if(disappearAtGoal)return;
  unsigned maxDuration(0.0);

  // Find max duration of all paths
  for(auto const& n:tree[location].paths){
    maxDuration=std::max(maxDuration,n->back().start.t);
  }
  if(maxDuration<BB::State::TIME_RESOLUTION_U)return;

  // Add wait actions (of 1 second) to goal typename BB::States less than max
  for(auto& n:tree[location].paths){
    while(n->back().start.t<maxDuration-BB::State::TIME_RESOLUTION_U){
      BB x(n->back());
      x.start.t=x.end.t;
      x.end.t+=BB::State::TIME_RESOLUTION_U;
      n->push_back(x);
    }
  }
}

// Loads conflicts into environements and returns the number of conflicts loaded.
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
unsigned CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::LoadConstraintsForNode(int location, int metaagent){
  // Select the unit from the tree with the new constraint
  int theMA(metaagent<0?tree[location].con.unit1:metaagent);
  unsigned numConflicts(0);

  // Reset the constraints in the test-environment
  ClearEnvironmentConstraints(theMA);

  // Add all of the constraints in the parents of the current node to the environment
  while(location!=0){
    if(theMA == tree[location].con.unit1)
    {
      numConflicts++;
      AddEnvironmentConstraint(tree[location].con.c.get(),theMA);
      if(verbose)std::cout << "Adding constraint (in accumulation)" << tree[location].con.c->start << "-->" << tree[location].con.c->end << " for MA " << theMA << "\n";
    }
    location = tree[location].parent;
  }// while (location != 0);
  return numConflicts;
}

// Attempts a bypass around the conflict using an alternate optimal path
// Returns whether the bypass was effective
// Note: should only be called for meta-agents with size=1
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
bool CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::Bypass(int best, std::pair<unsigned,unsigned> const& numConflicts, Conflict<BB> const& c1, unsigned otherunit, unsigned minTime)
{
  unsigned theUnit(activeMetaAgents[c1.unit1].units[0]);
  if(nobypass)return false;
  LoadConstraintsForNode(best,c1.unit1);
  AddEnvironmentConstraint(c1.c.get(),c1.unit1); // Add this constraint

  //std::cout << "Attempt to find a bypass.\n";

  bool success(false);
  Conflict<BB> c3, c4;
  std::vector<BB>* newPath(tree[best].paths[theUnit]);
  std::vector<int> newWpts(tree[best].wpts[theUnit]);
  // Re-perform the search with the same constraints (since the start and goal are the same)
  CBSUnit<BB,action,conflicttable,searchalgo> *c = (CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(c1.unit1);

  // Never use conflict avoidance tree for bypass
  bool orig(searchalgo::OpenList::Compare::useCAT);
  searchalgo::OpenList::Compare::useCAT=false;

  typename BB::State start(c->GetWaypoint(c1.prevWpt));
  typename BB::State goal(c->GetWaypoint(c1.prevWpt+1));
  // Preserve proper start time
  start.t = (*newPath)[newWpts[c1.prevWpt]].start.t;
  // Cost of the previous path
  double cost(currentEnvironment[theUnit]->environment->GetPathLength(*newPath));
  currentEnvironment[theUnit]->environment->setGoal(goal);
  std::vector<BB> path;
  
  Conflict<BB> t1,t2; // Temp variables
  // Perform search for the leg
  if(verbose)std::cout << "Bypass for unit " << theUnit << " on:\n";
  if(verbose)for(auto const& a:*newPath){std::cout << a << "\n";}
  if(verbose)std::cout << cost << " cost\n";
  if(verbose)std::cout << openList.top().nc << " conflicts\n";
  unsigned pnum(0);
  unsigned nc1(openList.top().nc);
  // Initialize A*, etc.
  Timer tmr;
  tmr.StartTimer();
  SetEnvironment(numConflicts.first,theUnit);
  astar.SetAgent(theUnit);
  astar.GetPath(currentEnvironment[theUnit]->environment,start,goal,path,minTime); // Get the path with the new constraint
  bypassplanTime+=tmr.EndTimer();
  if(path.size()==0){
    return false;
  }
  // This construction takes over the pointer to c1.c
  CBSTreeNode<BB,conflicttable> newNode(tree[best],c1,best,true,c1.unit1);
  MergeLeg<BB,action,conflicttable,searchalgo>(path,*newPath,newWpts,c1.prevWpt, c1.prevWpt+1,minTime);
  if(fleq(currentEnvironment[theUnit]->environment->GetPathLength(*newPath),cost)){
    do{
      pnum++;
      if(path.size()==0)continue;
      MergeLeg<BB,action,conflicttable,searchalgo>(path,*newPath,newWpts,c1.prevWpt, c1.prevWpt+1,minTime);
      newNode.path = *newPath;
      newNode.paths[theUnit] = &path;
      newNode.wpts[theUnit] = newWpts;

      if(verbose)for(auto const& a:*newPath){std::cout << a << "\n";}
      // TODO do full conflict count here
      auto pconf(FindHiPriConflict(newNode,c3,c4,false)); // false since we don't care about finding cardinal conflicts
      if(verbose)std::cout<<"Path number " << pnum << "\n";
      if(verbose)for(auto const& a:*newPath){std::cout << a << "\n";}
      if(verbose)std::cout << pconf.first << " conflicts\n";
      if(nc1>pconf.first){ // Is this bypass helpful?
        tree[best].path = *newPath;
        tree[best].paths[theUnit]=&tree[best].path;
        tree[best].wpts[theUnit]=newWpts;
        nc1=pconf.first;
        success=true;
      }
      if(pconf.first==0){
        if(verbose){std::cout << "BYPASS -- solution\n";}
        processSolution(timer->EndTimer());
        break;
      }
    }while(fleq(astar.GetNextPath(currentEnvironment[theUnit]->environment,start,goal,path,minTime),cost));
  }
  TOTAL_EXPANSIONS+=astar.GetNodesExpanded();
  if(killex != INT_MAX && TOTAL_EXPANSIONS>killex)
    processSolution(-timer->EndTimer());

  if(!success){
    // Give back the pointer to c1
    c1.c.reset(newNode.con.c.release());
    return false;
  }
  // Add CT node with the "best" bypass
  unsigned last = tree.size();
  tree.resize(last+1);
  tree[last] = newNode;
  cost=0;
  for (int y = 0; y < tree[last].paths.size(); y++){
    if(verbose){
      std::cout << "Agent " << y <<":\n";
      for(auto const& ff:*tree[last].paths[y]){
        std::cout << ff << "\n";
      }
      std::cout << "cost: " << currentEnvironment[theUnit]->environment->GetPathLength(*tree[last].paths[y]) << "\n";
    }
    cost += currentEnvironment[theUnit]->environment->GetPathLength(*tree[last].paths[y]);
  }
  if(verbose){
    std::cout << "New BYPASS NODE: " << last << " replanned: " << theUnit << " cost: " << cost << " " << nc1 << "\n";
  }
  openList.emplace(last, cost, nc1);

  searchalgo::OpenList::Compare::useCAT=orig;

  // Make sure that the current location is satisfiable
  if (newPath->size() == 0 && !(tree[best].paths[theUnit]->front() == tree[best].paths[theUnit]->back()))
  {
    return false;
  }

  return success;
}


/** Replan a node given a constraint */
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::Replan(int location)
{
  // Select the unit from the tree with the new constraint
  unsigned theMA(tree[location].con.unit1);
  if(activeMetaAgents[theMA].units.size()==1){
    unsigned theUnit(activeMetaAgents[theMA].units[0]);

    unsigned numConflicts(LoadConstraintsForNode(location));

    // Set the environment based on the number of conflicts
    SetEnvironment(numConflicts,theUnit); // This has to happen before calling LoadConstraints

    // Select the unit from the group
    CBSUnit<BB,action,conflicttable,searchalgo> *c((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(theUnit));

    // Retreive the unit start and goal
    //typename BB::State start, goal;
    //c->GetStart(start);
    //c->GetGoal(goal);

    // Recalculate the path
    //std::cout << "#conflicts for " << tempLocation << ": " << numConflicts << "\n";
    //currentEnvironment[theUnit]->environment->setGoal(goal);
    //std::cout << numConflicts << " conflicts " << " using " << currentEnvironment[theUnit]->environment->name() << " for agent: " << tree[location].con.unit1 << "?="<<c->getUnitNumber()<<"\n";
    //agentEnvs[c->getUnitNumber()]=currentEnvironment[theUnit]->environment;
    //astar.GetPath(currentEnvironment[theUnit]->environment, start, goal, thePath);
    //std::vector<BB> thePath(tree[location].paths[theUnit]);
    searchalgo::OpenList::Compare::openList=astar.GetOpenList();
    searchalgo::OpenList::Compare::currentEnv=(ConstrainedEnvironment<BB,action>*)currentEnvironment[theUnit]->environment;
    searchalgo::OpenList::Compare::currentAgent=theUnit;
    searchalgo::OpenList::Compare::CAT=&(tree[location].cat);
    searchalgo::OpenList::Compare::CAT->set(&tree[location].paths);

    if(searchalgo::OpenList::Compare::useCAT){
      searchalgo::OpenList::Compare::CAT->remove(*tree[location].paths[theUnit],currentEnvironment[theUnit]->environment,theUnit);
    }

    unsigned minTime(0);
    // If this is the last waypoint, the plan needs to extend so that the agent sits at the final goal
    if(tree[location].con.prevWpt+1==tree[location].wpts[theUnit].size()-1){
      minTime=GetMaxTime(location,theUnit)-1; // Take off a 1-second wait action, otherwise paths will grow over and over.
    }



    if(!quiet)std::cout << "Replan agent " << theUnit << "\n";
    //if(!quiet)std::cout << "re-planning path from " << start << " to " << goal << " on a path of len:" << thePath.size() << " out to time " << minTime <<"\n";
    astar.SetAgent(theUnit);
    ReplanLeg<BB,action,conflicttable,searchalgo>(c, astar, currentEnvironment[theUnit]->environment,*tree[location].paths[theUnit], tree[location].wpts[theUnit], tree[location].con.prevWpt, tree[location].con.prevWpt+1,minTime,theUnit);
    //for(int i(0); i<tree[location].paths.size(); ++i)
    //std::cout << "Replanned agent "<<i<<" path " << tree[location].paths[i].size() << "\n";

    if(killex != INT_MAX && TOTAL_EXPANSIONS>killex)
      processSolution(-timer->EndTimer());

    //DoHAStar(start, goal, thePath);
    //TOTAL_EXPANSIONS += astar.GetNodesExpanded();
    //std::cout << "Replan agent: " << location << " expansions: " << astar.GetNodesExpanded() << "\n";

    // Make sure that the current location is satisfiable
    if (tree[location].paths[theUnit]->size() < 1){
      tree[location].satisfiable = false;
    }

    // Add the path back to the tree (new constraint included)
    //tree[location].paths[theUnit].resize(0);
    if(searchalgo::OpenList::Compare::useCAT)
      searchalgo::OpenList::Compare::CAT->insert(*tree[location].paths[theUnit],currentEnvironment[theUnit]->environment,theUnit);

    /*for(int i(0); i<thePath.size(); ++i) {
      tree[location].paths[theUnit].push_back(thePath[i]);
      }*/
  }else{
    unsigned numConflicts(LoadConstraintsForNode(location));
    //AddEnvironmentConstraint(tree[location].con.c,theMA);
    std::vector<EnvironmentContainer<BB,action>*> envs(activeMetaAgents[theMA].units.size());
    MultiAgentState<typename BB::State> start(envs.size());
    MultiAgentState<typename BB::State> goal(envs.size());
    int i(0);
    for(auto theUnit: activeMetaAgents[theMA].units){
      envs[i]=currentEnvironment[theUnit];
      CBSUnit<BB,action,conflicttable,searchalgo> *c((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(theUnit));
      // TODO: Break out waypoints and plan each leg separately! (if possible!?!)
      start[i]=c->GetWaypoint(0);
      goal[i]=c->GetWaypoint(1);
      ++i;
    }
    Solution<BB> partial;
    maplanner maPlanner;
    //maPlanner.SetVerbose(verbose);
    maPlanner.quiet=quiet;
    Timer tmr;
    tmr.StartTimer();
    maPlanner.GetSolution(envs, start, goal, partial, activeMetaAgents[theMA].hint);
    maplanTime+=tmr.EndTimer();
    if(partial.size()){
      i=0;
      for(auto theUnit: activeMetaAgents[theMA].units){
        CBSUnit<BB,action,conflicttable,searchalgo> *c((CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(theUnit));
        unsigned minTime(GetMaxTime(location,theUnit)-1.0); // Take off a 1-second wait action, otherwise paths will grow over and over.
        unsigned wpt(0);
        /*for (unsigned l(0); l < partial[i].size(); l++)
          {
          tree[location].paths[theUnit].push_back(partial[i][l]);
          if(partial[i][l].sameLoc(c->GetWaypoint(tree[location].wpts[theUnit][wpt]))&&wpt<tree[location].wpts[theUnit].size()-1)wpt++;
          else tree[location].wpts[theUnit][wpt]=l;
          }*/
        MergeLeg<BB,action,conflicttable,searchalgo>(partial[i],*tree[location].paths[theUnit],tree[location].wpts[theUnit],0,1,minTime);
        ++i;

      // Make sure that the current location is satisfiable
      if (tree[location].paths[theUnit]->size() < 1){
        tree[location].satisfiable = false;
        break;
      }

      // Add the path back to the tree (new constraint included)
      //tree[location].paths[theUnit].resize(0);
      if(searchalgo::OpenList::Compare::useCAT)
        searchalgo::OpenList::Compare::CAT->insert(*tree[location].paths[theUnit],currentEnvironment[theUnit]->environment,theUnit);
    }
  }else{
    tree[location].satisfiable = false;
  }
}
}

// Returns a pair containing:
// Number of Conflicts (NC)
// and
// an enum:
// 0=no-conflict    0x0000
// 1=non-cardinal   0x0001
// 2=left-cardinal  0x0010
// 4=right-cardinal 0x0100
// 6=both-cardinal  0x0110
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
unsigned CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::HasConflict(std::vector<BB> const& a, std::vector<int> const& wa, std::vector<BB> const& b, std::vector<int> const& wb, int x, int y, Conflict<BB> &c1, Conflict<BB> &c2, std::pair<unsigned,unsigned>& conflict, bool update)
{
  // The conflict parameter contains the conflict count so far (conflict.first)
  // and the type of conflict found so far (conflict.second=BOTH_CARDINAL being the highest)

  // To check for conflicts, we loop through the timed actions, and check 
  // each bit to see if a constraint is violated
  int xmax(a.size());
  int ymax(b.size());
  unsigned orig(conflict.first); // Save off the original conflict count

  //if(verbose)std::cout << "Checking for conflicts between: "<<x << " and "<<y<<" ranging from:" << xmax <<"," << ymax << "\n";

  //CBSUnit<BB,action,conflicttable,searchalgo>* A = (CBSUnit<BB,action,conflicttable,searchalgo>*) this->GetMember(x);
  //CBSUnit<BB,action,conflicttable,searchalgo>* B = (CBSUnit<BB,action,conflicttable,searchalgo>*) this->GetMember(y);
  //std::cout << "x,y "<<x<<" "<<y<<"\n";

  signed pwptA(-1); // waypoint number directly after which a conflict occurs
  signed pwptB(-1);
  int pxTime(-1);
  int pyTime(-1);
  for (int i = 0, j = 0; j < ymax && i < xmax;) // If we've reached the end of one of the paths, then time is up and 
    // no more conflicts could occur
  {
    // I and J hold the current step in the path we are comparing. We need 
    // to check if the current I and J have a conflict, and if they do, then
    // we have to deal with it.

    // Figure out which indices we're comparing
    int xTime(max(0, min(i, xmax-1)));
    int yTime(max(0, min(j, ymax-1)));

    // Check if we're looking directly at a waypoint.
    // Increment so that we know we've passed it.
    //std::cout << "if(xTime != pxTime && A->GetWaypoint(pwptA+1)==a[xTime]){++pwptA; pxTime=xTime;}\n";
    //std::cout << " " << xTime << " " << pxTime << " " << pwptA;std::cout << " " << A->GetWaypoint(pwptA+1) << " " << a[xTime] << "==?" << (A->GetWaypoint(pwptA+1)==a[xTime]) <<  "\n";
    //std::cout << "if(yTime != pyTime && B->GetWaypoint(pwptB+1)==b[yTime]){++pwptB; pyTime=yTime;}\n";
    //std::cout << " " << yTime << " " << pyTime << " " << pwptB;std::cout << " " << B->GetWaypoint(pwptB+1) << " " << b[yTime] << "==?" << (B->GetWaypoint(pwptB+1)==b[yTime]) <<  "\n";
    if(xTime != pxTime && pwptA+2<wa.size() && xTime == wa[pwptA+1]){++pwptA; pxTime=xTime;}
    if(yTime != pyTime && pwptB+2<wb.size() && yTime == wb[pwptB+1]){++pwptB; pyTime=yTime;}

    //if(verbose)std::cout << "Looking at positions " << xTime <<":"<<a[xTime].t << "," << j<<":"<<b[yTime].t << std::endl;

    // Check the point constraints
    //Constraint<BB> x_c(a[xTime]);
    //typename BB::State y_c =b[yTime];


      typename BB::State const& aGoal(a[wa[pwptA+1]].end);
      typename BB::State const& bGoal(b[wb[pwptB+1]].end);
      collchecks++;
      if(collisionCheck3D(a[xTime].start,a[xTime].end,b[yTime].start,b[yTime].end,agentRadius)){
        ++conflict.first;
        if(verbose)std::cout<<conflict.first<<" conflicts; #"<<x<<":" << a[xTime]<<" #"<<y<<":"<<b[yTime]<<"\n";
        if(update && (BOTH_CARDINAL!=(conflict.second&BOTH_CARDINAL))){ // Keep searching until we find a both-cardinal conflict
          // Determine conflict type
          // If there are other legal successors with succ.f()=child.f(), this is non-cardinal
          unsigned conf(NO_CONFLICT); // Left is cardinal?
          {
            double childf(currentEnvironment[x]->environment->GCost(a[xTime].start,a[xTime].end)+currentEnvironment[x]->environment->HCost(a[xTime].end,aGoal));
            std::vector<typename BB::State> succ;
            currentEnvironment[x]->environment->GetSuccessors(a[xTime].start,succ);
            bool found(false);
            for(auto const& s:succ){ // Is there at least one successor with same g+h as child?
              if(s.sameLoc(a[xTime].end)){continue;}
              if(fleq(currentEnvironment[x]->environment->GCost(a[xTime].start,s)+currentEnvironment[x]->environment->HCost(s,aGoal),childf)){found=true;break;}
            }
            if(!found){conf|=LEFT_CARDINAL;}
          }
          // Right is cardinal
          {
            double childf(currentEnvironment[y]->environment->GCost(b[yTime].start,b[yTime].end)+currentEnvironment[y]->environment->HCost(b[yTime].end,bGoal));
            std::vector<typename BB::State> succ;
            currentEnvironment[y]->environment->GetSuccessors(b[yTime].start,succ);
            bool found(false);
            for(auto const& s:succ){ // Is there at least one successor with same g+h as child?
              if(s.sameLoc(b[yTime].end)){continue;}
              if(fleq(currentEnvironment[y]->environment->GCost(b[yTime].start,s)+currentEnvironment[y]->environment->HCost(s,bGoal),childf)){found=true;break;}
            }
            if(!found){conf|=RIGHT_CARDINAL;}
          }
          // Have we increased from non-cardinal to semi-cardinal or both-cardinal?
          if(NO_CONFLICT==conflict.second || ((conflict.second<=NON_CARDINAL)&&conf) || BOTH_CARDINAL==conf){
            conflict.second=conf+1;

            if(usecrossconstraints){
              c1.c.reset((Constraint<BB>*)new Collision<BB>(a[xTime].start, a[xTime].end, x));
              c2.c.reset((Constraint<BB>*)new Collision<BB>(b[yTime].start, b[yTime].end, y));
              c1.unit1 = y;
              c2.unit1 = x;
              c1.prevWpt = pwptB;
              c2.prevWpt = pwptA;
            }else{
              c1.c.reset((Constraint<BB>*)new Identical<BB>(a[xTime].start, a[xTime].end, x));
              c2.c.reset((Constraint<BB>*)new Identical<BB>(b[yTime].start, b[yTime].end, y));
              c1.unit1 = x;
              c2.unit1 = y;
              c1.prevWpt = pwptA;
              c2.prevWpt = pwptB;
            }

          }
        }
      }

    // Increment the counters based on the time

    // First we check to see if either is at the end
    // of the path. If so, immediately increment the 
    // other counter.
    if (i == xmax)
    {
      j++;
      continue;
    } else if (j == ymax)
    {
      i++;
      continue;
    }

    // Otherwise, we figure out which ends soonest, and
    // we increment that counter.
    if(a[xTime].end.t<b[yTime].end.t){
      // If the end-time of the x unit segment is before the end-time of the y unit segment
      // we have in increase the x unit but leave the y unit time the same
      i++;
    } else if(a[xTime].end.t==b[yTime].end.t){
      i++;
      j++;
    } else {
      // Otherwise, the y unit time has to be incremented
      j++;
    }

  } // End time loop
  return conflict.first-orig;
}

/** Find the highest priority conflict **/
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
std::pair<unsigned,unsigned> CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::FindHiPriConflict(CBSTreeNode<BB,conflicttable> const& location, Conflict<BB> &c1, Conflict<BB> &c2, bool update)
{
  if(verbose)std::cout<<"Checking for conflicts\n";
  // prefer cardinal conflicts
  std::pair<std::pair<unsigned,unsigned>,std::pair<Conflict<BB>,Conflict<BB>>> best;

  Timer tmr;
  tmr.StartTimer();
  for(unsigned a(0); a < activeMetaAgents.size(); ++a){
    for(unsigned b(a+1); b < activeMetaAgents.size(); ++b){
      unsigned intraConflicts(0); // Conflicts between meta-agents
      unsigned previous(best.first.second);
      // For each pair of units in the group
      for(unsigned x : activeMetaAgents.at(a).units){
        for(unsigned y : activeMetaAgents.at(b).units){
          // This call will update "best" with the number of conflicts and
          // with the *most* cardinal conflicts
          intraConflicts+=HasConflict(*location.paths[x],location.wpts[x],*location.paths[y],location.wpts[y],x,y,best.second.first,best.second.second,best.first,update);
          /*if(requireLOS&&currentEnvironment[x]->agentType==Map3D::air||currentEnvironment[y]->agentType==Map3D::air){
            if(ViolatesProximity(location.paths[x],location.paths[y]
          }*/
        }
      }
      // Make sure that the conflict counted is the one being returned (and being translated to meta-agent indices)
      if(best.first.second>previous && (intraConflicts)){
        if(usecrossconstraints){
          best.second.first.unit1=b;
          best.second.second.unit1=a;
        }else{
          best.second.first.unit1=a;
          best.second.second.unit1=b;
        }
      }
    }
  }
  collisionTime+=tmr.EndTimer();
  if(update && best.first.first){
    metaAgentConflictMatrix[best.second.first.unit1][best.second.second.unit1]++;
    c1=best.second.first;
    c2=best.second.second;
  }
  return best.first;
}

/** Draw the AIR CBS group */
template<typename BB, typename action,typename conflicttable, class maplanner, class searchalgo>
void CBSGroup<BB,action,conflicttable,maplanner,searchalgo>::OpenGLDraw(const ConstrainedEnvironment<BB,action> *ae, const SimulationInfo<typename BB::State,action,ConstrainedEnvironment<BB,action>> * sim)  const
{
	/*
	GLfloat r, g, b;
	glLineWidth(2.0);
	for (unsigned int x = 0; x < tree[bestNode].paths.size(); x++)
	{
		CBSUnit<BB,action,conflicttable,searchalgo> *unit = (CBSUnit<BB,action,conflicttable,searchalgo>*)this->GetMember(x);
		unit->GetColor(r, g, b);
		ae->SetColor(r, g, b);
		for (unsigned int y = 0; y < tree[bestNode].paths[x].size(); y++)
		{
			ae->OpenGLDraw(tree[bestNode].paths[x][y]);
		}
	}
	glLineWidth(1.0);
	*/
}


#endif /* defined(__hog2_glut__AirplaneCBSUnits__) */
