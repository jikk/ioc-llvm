#ifndef INFOAPP_H_
#define INFOAPP_H_

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/ADT/StringRef.h"

#include "Infoflow.h"

#include <set>

namespace deps {

using namespace llvm;

class InfoAppPass : public ModulePass {  
  public:
  InfoAppPass() : ModulePass(ID) {}
  static char ID;
  bool runOnModule(Module &M);
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<Infoflow>();
    AU.setPreservesAll();
  }

  private:
    Infoflow* infoflow;
    DenseMap<const Value*, bool> xformMap;
    std::set<StringRef> whiteSet;
    std::set<StringRef> blackSet;

    virtual void doInitialization();
    virtual void doFinalization();
  
    bool TrackSoln(Module &M, InfoflowSolution* soln);
    bool checkTainted(Value &V, InfoflowSolution* soln);
  
};  // class

/* ID for InfoAppPass */
char InfoAppPass::ID = 99;

static RegisterPass<InfoAppPass>
XX ("infoapp", "implements infoapp", true, true);

}  // nameapce

#endif
