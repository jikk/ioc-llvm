#include <sstream>
#include <fstream>
#include <string>

#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "Infoflow.h"
#include "Slice.h"

#include "infoapp.h"
//#define __REACH__

//#define __DBG__
#define DBG_LINE 322
#define DBG_COL 23

using std::set;

using namespace llvm;
using namespace deps;

static void getWhiteList();

namespace {

  void
  InfoAppPass::format_ioc_report_func(const Value* val, raw_string_ostream& rs)
  {
  
  const CallInst* ci = dyn_cast<CallInst>(val);
  assert(ci && "CallInst casting check");

  const Function* func = ci->getCalledFunction();
  assert(func && "Function casting check");
  
  //line & column
  uint64_t line = getIntFromVal(ci->getOperand(0));
  uint64_t col = getIntFromVal(ci->getOperand(1));

  //XXX: restructure
  std::string fname = "";
  getStringFromVal(ci->getOperand(2), fname);
    
  rs << func->getName().str() << ":";
  rs << fname << ":" ;
  rs << " (line ";
  rs << line;
  rs << ", col ";
  rs << col << ")";

  //ioc_report_* specific items
  if (func->getName() == "__ioc_report_add_overflow" ||
      func->getName() == "__ioc_report_sub_overflow" ||
      func->getName() == "__ioc_report_mul_overflow" ||
      func->getName() == "__ioc_report_shr_bitwidth" ||
      func->getName() == "__ioc_report_shl_bitwidth" ||
      func->getName() == "__ioc_report_shl_strict")
  {
    ;
  } else if (func->getName() == "__ioc_report_conversion") {
    ;
  } else {
;
//    assert(! "invalid function name");
  }
}

static const struct CallTaintEntry bLstSourceSummaries[] = {
  // function  tainted values   tainted direct memory tainted root ptrs
  { "fgets",   TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { 0,         TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING }
};

static const struct CallTaintEntry wLstSourceSummaries[] = {
  // function  tainted values   tainted direct memory tainted root ptrs
  { "gettimeofday",   TAINTS_RETURN_VAL,  TAINTS_ARG_1,      TAINTS_NOTHING },
  { 0,                TAINTS_NOTHING,     TAINTS_NOTHING,    TAINTS_NOTHING }
};

static rmChecks *rmCheckList;
  
CallTaintEntry nothing = { 0, TAINTS_NOTHING, TAINTS_NOTHING, TAINTS_NOTHING };
  
//XXX: same function defined from SourceSinkAnalysis
static const CallTaintEntry *
findEntryForFunction(const CallTaintEntry *Summaries,
                     const std::string &FuncName) {
  unsigned Index;
  
  if (StringRef(FuncName).startswith("__ioc"))
    return &nothing;
  
  for (Index = 0; Summaries[Index].Name; ++Index) {
    if (Summaries[Index].Name == FuncName)
      return &Summaries[Index];
  }
  // Return the default summary.
  return &Summaries[Index];
}
  
void
InfoAppPass::doInitialization() {
  getWhiteList();
  infoflow = &getAnalysis<Infoflow>();
  DEBUG(errs() << "[InfoApp] doInitialization\n");
}
  
void
InfoAppPass::doFinalization() {
  DEBUG(errs() << "[InfoApp] doFinalization\n");
  DenseMap<const Value*, bool>::const_iterator xi = xformMap.begin();
  DenseMap<const Value*, bool>::const_iterator xe = xformMap.end();


  for (;xi!=xe; xi++) {
    std::string output;
    raw_string_ostream rs(output);
    format_ioc_report_func(xi->first, rs);
    
    //changed ones
    errs() << "[InfoApp]xformMap:" << xi->second << ":";
    errs() << rs.str();
    errs() << "\n";
  }
  for (unsigned i=0; rmCheckList[i].func; i++) {
    delete rmCheckList[i].func;
    delete rmCheckList[i].fname;
  }
  delete rmCheckList;
}

bool
InfoAppPass::runOnModule(Module &M) {
  //assigning unique IDs to each overflow locations.
  static uint64_t unique_id = 0;

  doInitialization();

  for (Module::iterator mi = M.begin(); mi != M.end(); mi++) {
    Function& F = *mi;
    //XXX: implement something here ..

    errs() << "DBG0:fname:" << F.getName() << "\n";
    removeChecksForFunction(F, M);
    
    for (Function::iterator bi = F.begin(); bi != F.end(); bi++) {
      BasicBlock& B = *bi;
      for (BasicBlock::iterator ii = B.begin(); ii !=B.end(); ii++) {
        if (CallInst* ci = dyn_cast<CallInst>(ii)) {
          
          /*
           List of overflow report functions

           LVal: 4th arg, RVal: 5th arg
           __ioc_report_add_overflow()
           __ioc_report_sub_overflow()
           __ioc_report_mul_overflow()
           
           Val: 7th arg
           __ioc_report_conversion()
           */
          
          /*
           XXX: this more about the following functions
           __ioc_report_div_error()
           __ioc_report_rem_error()
           __ioc_report_shl_bitwidth()
           __ioc_report_shl_strict()
           */
          
          Function* func = ci->getCalledFunction();
          if (!func)
            continue;
          
          if (func->getName() == "__ioc_report_add_overflow" ||
              func->getName() == "__ioc_report_sub_overflow" ||
              func->getName() == "__ioc_report_mul_overflow" ||
              func->getName() == "__ioc_report_shr_bitwidth" ||
              func->getName() == "__ioc_report_shl_bitwidth" ||
              func->getName() == "__ioc_report_shl_strict")
          {
#ifdef __DBG__
            uint32_t line = getIntFromVal(ci->getOperand(0));
            uint32_t col  = getIntFromVal(ci->getOperand(1));
            
            if (line != DBG_LINE || col != DBG_COL)
              continue;
#endif
            
            //check for arg. count
            assert(ci->getNumOperands() == 8);
            DEBUG(errs() << "[InfoApp]numOper:" << ci->getNumOperands() << "\n");
            DEBUG(errs() << "[InfoApp]func_name:" << func->getName() << "\n");
  
            std::stringstream SS;
            std::set<std::string> kinds;
            
            SS << unique_id++;
            std::string sinkKind = "overflow" + SS.str();
            
            Value* lval = ci->getOperand(4);
            Value* rval = ci->getOperand(5);

            //tagging lVal
            infoflow->setUntainted(sinkKind, *lval);
            
            //tagging rVal
            infoflow->setUntainted(sinkKind, *rval);

            kinds.insert(sinkKind);
            InfoflowSolution* soln = infoflow->greatestSolution(kinds, false);
            
	    //check for simple const. assignment
            //getting valeMap
            std::set<const Value *> vMap;
            soln->getValueMap(vMap);
            
            if(isConstAssign(vMap))
            {
              //replace it for simple const. assignment
              DEBUG(errs() << "[InfoApp]isConstAssign0:true" << "\n");
              xformMap[ci] = true;
              
            } else {
              xformMap[ci] = trackSoln(M, soln, ci, sinkKind);
            }
	    if (xformMap[ci]) {
		//benign function. replace it.
            	FunctionType *ftype = func->getFunctionType();
            	std::string fname = "__ioc_" + std::string(func->getName());
            
            	Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                	                                  ftype,
                                                          func->getAttributes());
            
            	ci->setCalledFunction(ioc_wrapper);
		
	    }
            
          } else if (func->getName() == "__ioc_report_conversion") {
            //check for arg. count
            assert(ci->getNumOperands() == 10);
            
#ifdef __DBG__
            uint32_t line = getIntFromVal(ci->getOperand(0));
            uint32_t col  = getIntFromVal(ci->getOperand(1));
            
            if (line != DBG_LINE || col != DBG_COL)
              continue;
#endif
            
            std::stringstream SS;
            std::set<std::string> kinds;
            
            SS << unique_id++;
            std::string sinkKind = "overflow" + SS.str();
            
            Value* val = ci->getOperand(7);

            //tagging unary arg
            infoflow->setUntainted(sinkKind, *val);

            kinds.insert(sinkKind);
            InfoflowSolution* soln = infoflow->greatestSolution(kinds, false);
            
            //check for simple const. assignment
            //getting valeMap
            std::set<const Value *> vMap;
            soln->getValueMap(vMap);

            if(isConstAssign(vMap))
            {
              //replace it for simple const. assignment
              DEBUG(errs() << "[InfoApp]isConstAssign1:true" << "\n");
              xformMap[ci] = true;

            } else {
              xformMap[ci] = trackSoln(M, soln, ci, sinkKind);
            }
	    
	    if (xformMap[ci]) {
		//benign function. replace it.
            	FunctionType *ftype = func->getFunctionType();
            	std::string fname = "__ioc_" + std::string(func->getName());
            
            	Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                	                                  ftype,
                                                          func->getAttributes());
            
            	ci->setCalledFunction(ioc_wrapper);
		
	    }
          } else if ((func->getName() == "div")   ||
                     (func->getName() == "ldiv")  ||
                     (func->getName() == "lldiv") ||
                     (func->getName() == "iconv")
                     ) {
            
            FunctionType *ftype = func->getFunctionType();
            std::string fname = "__ioc_" + std::string(func->getName());
            
            Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                                                       ftype,
                                                       func->getAttributes());
            
            ci->setCalledFunction(ioc_wrapper);
          }
        }
      }
    }
  }
  doFinalization();
  return false;
}

//XXX: now it is too messy. the function need some clean-up  
bool
InfoAppPass::trackSoln(Module &M,
                        InfoflowSolution* soln,
                        CallInst* sinkCI,
                        std::string& kind)
{
  DEBUG(errs() << "[InfoApp]trackSoln:" << "\n");
  //by default do not change/replace.
  bool ret = false;
    
  //need optimization or parallelization
  for (Module::iterator mi = M.begin(); mi != M.end(); mi++) {
    Function& F = *mi;
  
    if (F.getName() != (sinkCI->getParent()->getParent()->getName())) {
      continue;
    }
    for (Function::iterator bi = F.begin(); bi != F.end(); bi++) {
      BasicBlock& B = *bi;
      for (BasicBlock::iterator ii = B.begin(); ii !=B.end(); ii++) {
        //instruction is tainted
        if (checkBackwardTainted(*ii, soln)) {
          DEBUG(errs() << "[InfoApp]checkBackwardTainted:");
          DEBUG(ii->dump());
          
          if (CallInst* ci = dyn_cast<CallInst>(ii)) {
            Function* func = ci->getCalledFunction();

            if (!func) continue;
            
            
            //check for white-listing
            const CallTaintEntry *entry =
              findEntryForFunction(wLstSourceSummaries, func->getName());

            if (entry->Name) {
              DEBUG(errs() << "[InfoApp]white-list:" << func->getName() <<"\n");
              std::set<std::string> kinds;
              std::string srcKind = "src0" + kind;
              kinds.insert(srcKind);
              
              //TODO: need per white-list entry setting required
              const CallTaintSummary* vSum = &(entry->ValueSummary);
              const CallTaintSummary* dSum = &(entry->DirectPointerSummary);
              const CallTaintSummary* rSum = &(entry->RootPointerSummary);
              
              //vSum
              if (vSum->TaintsReturnValue) {
                infoflow->setTainted(srcKind, *ci);
              }

              for (unsigned ArgIndex = 0;
                   ArgIndex < vSum->NumArguments;
                   ++ArgIndex) {
                
                if (vSum->TaintsArgument[ArgIndex]) {
                  infoflow->setTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }
              
              //dSum
              if (dSum->TaintsReturnValue) {
                infoflow->setDirectPtrTainted(srcKind, *ci);
              }
              
              for (unsigned ArgIndex = 0;
                   ArgIndex < dSum->NumArguments;
                   ++ArgIndex) {
                
                if (dSum->TaintsArgument[ArgIndex]) {
                  infoflow->setDirectPtrTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }

              //rSum
              if (rSum->TaintsReturnValue) {
                infoflow->setReachPtrTainted(srcKind, *ci);
              }
              
              for (unsigned ArgIndex = 0;
                   ArgIndex < rSum->NumArguments;
                   ++ArgIndex) {
                
                if (rSum->TaintsArgument[ArgIndex]) {
                  infoflow->setReachPtrTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }
            
              //trace-back to confirm infoflow with forward slicing
              //explicit-flow and cutAfterSinks.
              InfoflowSolution* fsoln =
                infoflow->leastSolution(kinds, false, true);              
              
              Function* sinkFunc = sinkCI->getCalledFunction();
              if(sinkFunc->getName() == "__ioc_report_conversion") {
                
                if (checkForwardTainted(*(sinkCI->getOperand(7)), fsoln)) {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:white0: true\n");
                  ret = true;
                } else {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:white0: false\n");
                  ret = false;
                }

              } else if (sinkFunc->getName() == "__ioc_report_add_overflow" ||
                         sinkFunc->getName() == "__ioc_report_sub_overflow" ||
                         sinkFunc->getName() == "__ioc_report_mul_overflow" ||
                         sinkFunc->getName() == "__ioc_report_shr_bitwidth" ||
                         sinkFunc->getName() == "__ioc_report_shl_bitwidth" ||
                         sinkFunc->getName() == "__ioc_report_shl_strict")
              {
                if (checkForwardTainted(*(sinkCI->getOperand(4)), fsoln) ||
                    checkForwardTainted(*(sinkCI->getOperand(5)), fsoln)) {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:white1: true\n");
                  ret = true;
                } else {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:white1: false\n");
                  ret = false;
                }

              } else {
                assert(false && "not __ioc_report function");
              }
            }

            //check for black-listing
            entry =
              findEntryForFunction(bLstSourceSummaries, func->getName());

            if (entry->Name) {
              DEBUG(errs() << "[InfoApp]black-list:" << func->getName() <<"\n");
              std::set<std::string> kinds;
              std::string srcKind = "src1" + kind;
              kinds.insert(srcKind);
              
              //TODO: need per white-list entry setting required
              const CallTaintSummary* vSum = &(entry->ValueSummary);
              const CallTaintSummary* dSum = &(entry->DirectPointerSummary);
              const CallTaintSummary* rSum = &(entry->RootPointerSummary);
              
              //vSum
              if (vSum->TaintsReturnValue) {
                infoflow->setTainted(srcKind, *ci);
              }
              
              for (unsigned ArgIndex = 0;
                   ArgIndex < vSum->NumArguments;
                   ++ArgIndex) {
                
                if (vSum->TaintsArgument[ArgIndex]) {
                  infoflow->setTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }
              
              //dSum
              if (dSum->TaintsReturnValue) {
                infoflow->setDirectPtrTainted(srcKind, *ci);
              }
              
              for (unsigned ArgIndex = 0;
                   ArgIndex < dSum->NumArguments;
                   ++ArgIndex) {

                if (dSum->TaintsArgument[ArgIndex]) {
                  infoflow->setDirectPtrTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }
              
              //rSum
              if (rSum->TaintsReturnValue) {
                infoflow->setReachPtrTainted(srcKind, *ci);
              }
              
              for (unsigned ArgIndex = 0;
                   ArgIndex < rSum->NumArguments;
                   ++ArgIndex) {
                
                if (rSum->TaintsArgument[ArgIndex]) {
                  infoflow->setReachPtrTainted(srcKind, *(ci->getOperand(ArgIndex)));
                }
              }
              
              //trace-back to confirm infoflow with forward slicing
              //explicit-flow and cutAfterSinks.
              InfoflowSolution* fsoln =
              infoflow->leastSolution(kinds, false, true);
              
              Function* sinkFunc = sinkCI->getCalledFunction();
              if(sinkFunc->getName() == "__ioc_report_conversion") {
                if (checkForwardTainted(*(sinkCI->getOperand(7)), fsoln)) {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:black0: true\n");
                  //tainted source detected! just get out
                  return false;
                } else {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:black0: false\n");
                }
                
              } else if (sinkFunc->getName() == "__ioc_report_add_overflow" ||
                         sinkFunc->getName() == "__ioc_report_sub_overflow" ||
                         sinkFunc->getName() == "__ioc_report_mul_overflow" ||
                         sinkFunc->getName() == "__ioc_report_shr_bitwidth" ||
                         sinkFunc->getName() == "__ioc_report_shl_bitwidth" ||
                         sinkFunc->getName() == "__ioc_report_shl_strict")
              {
                if (checkForwardTainted(*(sinkCI->getOperand(4)), fsoln) ||
                    checkForwardTainted(*(sinkCI->getOperand(5)), fsoln))
                {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:black1: true\n");
                  return false;
                } else {
                  DEBUG(errs() << "[InfoApp]checkForwardTainted:black1: false\n");
                }
              } else {
                assert(false && "not __ioc_report function");
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

bool
InfoAppPass::checkBackwardTainted(Value &V, InfoflowSolution* soln, bool direct)
{
  bool ret = (!soln->isTainted(V));
  
  if (direct) {
    ret = ret || (!soln->isDirectPtrTainted(V));
#ifdef __REACH__
    // XXX: not sure about Reachable pointer sets.
    ret = || (!soln->isReachPtrTainted(V));
#endif
  }

  return ret;
}
  
bool
InfoAppPass::checkForwardTainted(Value &V, InfoflowSolution* soln, bool direct)
{
  bool ret = (soln->isTainted(V));

  if (direct) {
    ret = ret || (soln->isDirectPtrTainted(V));
#ifdef __REACH__
    // XXX: not sure about Reachable pointer sets.
    ret = || (soln->isReachPtrTainted(V));
#endif
  }
  
  return ret;
}

bool
InfoAppPass::isConstAssign(const std::set<const Value *> vMap) {
  std::set<const Value *>::const_iterator vi = vMap.begin();
  std::set<const Value *>::const_iterator ve = vMap.end();

  for (;vi!=ve; vi++) {
    const Value* val = (const Value*) *vi;
    if (const CallInst* ci = dyn_cast<const CallInst>(val)) {
      Function* func = ci->getCalledFunction();
      //assert(func && "func should be fine!");
      if (func && func->getName().startswith("llvm.ssub.with.overflow")) {
        continue;
      } else {
        //XXX: need more for other function calls
        DEBUG(errs() << "[InfoApp]isConstAssign:" << func->getName() <<"\n");
        return false;
      }
    } else if (dyn_cast<const LoadInst>(val)) {
      return false;
    } else {
        //XXX: need more for other instructions
    }
  }
  return true;
}
  
void
InfoAppPass::removeChecksForFunction(Function& F, Module& M) {
  for (unsigned i=0; rmCheckList[i].func; i++) {
    if (F.getName() == rmCheckList[i].func) {
      //errs() << "DBG0:"<< F.getName() << ":" << i << "\n";
      for (Function::iterator bi = F.begin(); bi != F.end(); bi++) {
        BasicBlock& B = *bi;
        for (BasicBlock::iterator ii = B.begin(); ii !=B.end(); ii++) {
          if (CallInst* ci = dyn_cast<CallInst>(ii)) {
            Function* func = ci->getCalledFunction();
            if (!func)
              continue;

            if (rmCheckList[i].overflow) {
              if((func->getName() == "__ioc_report_add_overflow") ||
                 (func->getName() == "__ioc_report_sub_overflow") ||
                 (func->getName() == "__ioc_report_mul_overflow")
                 ) {
                xformMap[ci] = true;
                //benign function. replace it.
                FunctionType *ftype = func->getFunctionType();
                std::string fname = "__ioc_" + std::string(func->getName());
            
                Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                                                              ftype,
                                                              func->getAttributes());
            
            	ci->setCalledFunction(ioc_wrapper);
              }
            }
            
            if (rmCheckList[i].conversion) {
              if((func->getName() == "__ioc_report_conversion")) {
                xformMap[ci] = true;
		//benign function. replace it.
            	FunctionType *ftype = func->getFunctionType();
            	std::string fname = "__ioc_" + std::string(func->getName());
            
            	Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                	                                  ftype,
                                                          func->getAttributes());
            
            	ci->setCalledFunction(ioc_wrapper);
              }
            }

            if (rmCheckList[i].shift) {
              if((func->getName() == "__ioc_report_shr_bitwidth") ||
                 (func->getName() == "__ioc_report_shl_bitwidth") ||
                 (func->getName() == "__ioc_report_shl_strict")
                 ) {
                xformMap[ci] = true;
		//benign function. replace it.
            	FunctionType *ftype = func->getFunctionType();
            	std::string fname = "__ioc_" + std::string(func->getName());
            
            	Constant* ioc_wrapper = M.getOrInsertFunction(fname,
                	                                  ftype,
                                                          func->getAttributes());
            
            	ci->setCalledFunction(ioc_wrapper);
              }
            }
          }
        }
      }
    }
  }
}

uint64_t
InfoAppPass::getIntFromVal(Value* val) {
  ConstantInt* num = dyn_cast<ConstantInt>(val);
  assert(num && "constant int casting check");
  return num->getZExtValue();
}

void
  InfoAppPass::getStringFromVal(Value* val, std::string& output) {
  Constant* gep = dyn_cast<Constant>(val);
  assert(gep && "assertion");
  GlobalVariable* global = dyn_cast<GlobalVariable>(gep->getOperand(0));
  assert(global && "assertion");
  ConstantDataArray* array = dyn_cast<ConstantDataArray>(global->getInitializer());
  if (array->isCString()) {
    output = array->getAsCString();
  }
}
  
}  //namespace deps

namespace  {
  /* ID for InfoAppPass */
  char InfoAppPass::ID = 0;
  
  static RegisterPass<InfoAppPass>
  XX ("infoapp", "implements infoapp", true, true);
  
  
static void initializeInfoAppPasses(PassRegistry &Registry) {
  llvm::initializeAllocIdentifyPass(Registry);
  llvm::initializePDTCachePass(Registry);
}
  
static void registerInfoAppPasses(const PassManagerBuilder &, PassManagerBase &PM)
{
  PM.add(llvm::createPromoteMemoryToRegisterPass());
  PM.add(llvm::createPDTCachePass());
  PM.add(new InfoAppPass());
}
  
//static RegisterStandardPasses
//  RegisterInfoAppPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
//                      registerInfoAppPasses);
  
//static RegisterStandardPasses
//  RegisterInfoAppPass(PassManagerBuilder::EP_LoopOptimizerEnd,
//                      registerInfoAppPasses);


  
class StaticInitializer {
public:
  StaticInitializer() {
    char* passend = getenv("__PASSEND__");
    
    if (passend) {
      errs() << "== EP_LoopOptimizerEnd ==\n";
      RegisterStandardPasses
      RegisterInfoAppPass(PassManagerBuilder::EP_LoopOptimizerEnd,
                        registerInfoAppPasses);
    } else {
      errs() << "== EP_ModuleOptimizerEarly\n ==";
      RegisterStandardPasses
      RegisterInfoAppPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
                          registerInfoAppPasses);
    }
    
    
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeInfoAppPasses(Registry);
  }
};
static StaticInitializer InitializeEverything;

}

using namespace std;

static void
getWhiteList() {
  string line, file, function, conv;
  string overflow, shift;
  bool conv_bool, overflow_bool, shift_bool;
  unsigned numLines;
  unsigned i;
  unsigned pos = 0;
  ifstream whitelistFile;
  whitelistFile.open(WHITE_LIST);
  //get number of lines
  numLines = 0;
  while (whitelistFile.good()) {
    getline(whitelistFile, line);
    if (!line.empty())
      numLines++;
  }

  whitelistFile.clear();
  whitelistFile.seekg(0, ios::beg);

  rmCheckList = new rmChecks[numLines];
  for (i = 0; i < numLines; i++) {
    getline(whitelistFile, line);
    //handle each line
    pos = 0;
    function = line.substr(pos, line.find(","));
    pos = line.find(",") + 1;
    file = line.substr(pos, line.find(",", pos) - pos);
    pos = line.find(",", pos) + 1;
    conv = line.substr(pos, line.find(",", pos) - pos);
    pos = line.find(",", pos) + 1;
    overflow = line.substr(pos, line.find(",", pos) - pos);
    pos = line.find(",", pos) + 1;
    shift = line.substr(pos, line.size() - pos);

    if (conv.compare("true") == 0)
      conv_bool = true;
    else
      conv_bool = false;

    if (overflow.compare("true") == 0)
      overflow_bool = true;
    else
      overflow_bool = false;

    if (shift.compare("true") == 0)
      shift_bool = true;
    else
      shift_bool = false;

    if (function.compare("0") == 0)
      rmCheckList[i].func = (char*) 0;
    else {
      rmCheckList[i].func = new char[strlen(function.c_str())+1];
      for (unsigned j = 0; j < strlen(function.c_str()); j++)
        rmCheckList[i].func[j] = function[j];
      rmCheckList[i].func[strlen(function.c_str())] = '\0';
    }
    if (file.compare("0") == 0)
      rmCheckList[i].fname =  (char *) 0;
    else {
      rmCheckList[i].fname = new char[strlen(file.c_str()) +1];
      for (unsigned j = 0; j < strlen(file.c_str()); j++)
        rmCheckList[i].fname[j] = file[j];
      rmCheckList[i].fname[strlen(file.c_str())] = '\0';

    }
    rmCheckList[i].conversion = conv_bool;
    rmCheckList[i].overflow = overflow_bool;
    rmCheckList[i].shift = shift_bool;

  }
  whitelistFile.close();
}
