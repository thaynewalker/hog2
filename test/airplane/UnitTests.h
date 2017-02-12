#include "AirplanePerimeterBuilder.h"
#include "Airplane.h"
#include "AirplaneGridless.h"
#include <gtest/gtest.h>

template<typename environ, typename state>
void TestReverseSuccessors(environ& env, state const& s, state const& g, state o) {
  //std::cout << "original " << o << "\n";
  std::vector<state> ancestors;
  env.GetReverseSuccessors(o,ancestors);
  for (auto &a : ancestors)
  {
    //std::cout << " Ancestor " << a << "\n";
    // All ancestors should have a successor that is equal to the original state
    std::vector<state> successors;
    env.GetSuccessors(a,successors);
    bool found(false);
    for(auto &c: successors){
      //std::cout << "        " << c << "\n";
      if(c==o){found=true; break;}
    }
    ASSERT_TRUE(found);
  }
}

TEST(AirplaneEnvironmentTest, GetActions) { 
  AirplaneEnvironment env;
  env.loadPerimeterDB();
  airplaneAction a(0,0,0);
  airplaneState s(50,50,16,3,0);
  airplaneState g(50,40,18,3,0);
  env.setGoal(g);
  std::vector<airplaneAction> actions;
  env.GetActions(s,actions);
  std::vector<airplaneAction> ractions;
  env.GetReverseActions(s,ractions);
  ASSERT_EQ(63, actions.size());
  ASSERT_EQ(63, ractions.size());

  for (auto &a : actions)
  {
    airplaneState s1=s;
    env.ApplyAction(s,a);
    airplaneAction a2(env.GetAction(s1,s));
    env.UndoAction(s,a);
    ASSERT_EQ(s,s1);
    ASSERT_EQ(a,a2);
    s=s1; // Reset
  }
  env.setStart(s);
  env.setGoal(g);
  airplaneState o(56,44,11,3,0);
  TestReverseSuccessors(env,s,g,o);
  o=s;
  o.x+=1;
  TestReverseSuccessors(env,s,g,o);
  o=g;
  o.x+=1;
  TestReverseSuccessors(env,s,g,o);
}

TEST(GridlessEnvironmentTest, GetActions) { 
  AirplaneGridlessEnvironment env;
  PlatformAction a(0,0,0);
  PlatformState s(50,50,16,0,0,3);
  PlatformState g(50,40,18,0,0,3);
  env.setGoal(g);
  std::vector<PlatformAction> actions;
  env.GetActions(s,actions);
  std::vector<PlatformAction> ractions;
  env.GetReverseActions(s,ractions);
  ASSERT_EQ(45, actions.size());
  ASSERT_EQ(45, ractions.size());

  for (auto &a : actions)
  {
    PlatformState s1=s;
    env.ApplyAction(s,a);
    ASSERT_FALSE(s==s1);
    PlatformAction a2(env.GetAction(s1,s));
    std::cout <<a;
    std::cout <<a2;
    env.UndoAction(s,a);
    ASSERT_EQ(s,s1);
    ASSERT_EQ(a,a2);
    s=s1; // Reset
  }
  env.setStart(s);
  env.setGoal(g);
  PlatformState o(56,44,11,0,0,3);
  TestReverseSuccessors(env,s,g,o);
  o=s;
  o.x+=1;
  TestReverseSuccessors(env,s,g,o);
  o=g;
  o.x+=1;
  TestReverseSuccessors(env,s,g,o);
}

TEST(HeuristicTest, LearnSpace) { 
  AirplaneEnvironment env;
  env.setGoal(airtimeState());
  AirplanePerimeterBuilder<airplaneState,airplaneAction,AirplaneEnvironment,5> builder;
  std::vector<airplaneState> states;
  states.emplace_back(40,40,10,3,0);
  builder.learnSpace(env,states,5);
  airplaneState s(40,39,10,3,0);
  airplaneState s2(40,41,10,3,0);
  std::cout << env.GCost(s,states[0]) << "\n";
  std::cout << builder.GCost(s,states[0],env) << "\n";
  std::cout << builder.GCost(s2,states[0],env) << "\n";
}

TEST(HeuristicTest, HashTest) { 
  AirplaneEnvironment env;
  env.setGoal(airtimeState());
  AirplanePerimeterBuilder<airplaneState,airplaneAction,AirplaneEnvironment,5> builder;
  for(int i(0); i<1000; ++i){
    std::set<uint64_t> hashes;
    airplaneState start(rand() % 80, rand() % 80, rand() % 20, rand() % 5 + 1, rand() % 8, false);
    airplaneState goal(rand() % 80, rand() % 80, rand() % 20, rand() % 5 + 1, rand() % 8, false);
    std::vector<double> data(5);
    env.GetDifference(start,goal,data);
    uint64_t hash(builder.getHash(data,env.GetRanges()));
    std::vector<double> reversed(5);
    builder.fromHash(reversed,hash,env.GetRanges());
    ASSERT_EQ(data,reversed);
  }
  //std::cout << builder.GCost(s,states[0],env) << "\n";
  //std::cout << builder.GCost(s2,states[0],env) << "\n";
}

TEST(HeuristicTest, LoadTest) { 
  AirplaneEnvironment env;
  env.setGoal(airtimeState());
  AirplanePerimeterBuilder<airplaneState,airplaneAction,AirplaneEnvironment,5> builder;
  std::vector<airplaneState> states;
  for(int s=3; s<4; ++s){
    for(int h=0; h<1; ++h){
      states.emplace_back(40,40,10,s,h);
    }
  }
  builder.loadDB(env,states,5,99,3,99,"airplane",true); // Force build
  {
    airplaneState g(40,40,10,3,0);
    airplaneState s(40,41,10,3,0);
    airplaneState s2(40,39,10,3,0);
    builder.loadDB(env,states,5,99,3,99,"airplane");
    ASSERT_EQ(.006,builder.GCost(s,g,env));
    ASSERT_EQ(.03,builder.GCost(s2,g,env));
    ASSERT_EQ(.012,builder.GCost(s,s2,env));
  }
}

TEST(HeuristicTest, GetTestTest) { 
  AirplaneEnvironment env;
  env.setGoal(airtimeState());
  AirplanePerimeterBuilder<airplaneState,airplaneAction,AirplaneEnvironment,5> builder;
  std::vector<airplaneState> states;
  states.emplace_back(40,40,10,3,0);
  airplaneState s(40,40,10,3,0);
  airplaneState g(44,40,10,3,0);
  builder.loadDB(env,states,5,99,3,99,"airplane");
  ASSERT_EQ(.006,builder.GCost(s,g,env));
}
