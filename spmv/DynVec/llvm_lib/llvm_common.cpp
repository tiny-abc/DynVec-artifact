#include <atomic>
#include <mutex>
#include "llvm_common.h"
#include <sstream>

struct LLVMEnv {
  std::mutex mu;
  std::atomic<bool> all_initialized{false};

  static LLVMEnv* Global() {
    static LLVMEnv inst;
    return &inst;
  }
};


void InitializeLLVM() {
  LLVMEnv* e = LLVMEnv::Global();
  if (!e->all_initialized.load(std::memory_order::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(e->mu);
    if (!e->all_initialized.load(std::memory_order::memory_order_acquire)) {
      llvm::InitializeAllTargetInfos();
      llvm::InitializeAllTargets();
      llvm::InitializeAllTargetMCs();
      llvm::InitializeAllAsmParsers();
      llvm::InitializeAllAsmPrinters();
      e->all_initialized.store(true, std::memory_order::memory_order_release);
    }
  }
}
void ParseLLVMTargetOptions(const std::string& target_str,
                            std::string* triple,
                            std::string* mcpu,
                            std::string* mattr,
                            llvm::TargetOptions* options) {
  // setup target triple
  size_t start = 0;
  if (target_str.length() >= 4 &&
      target_str.substr(0, 4) == "llvm") {
    start = 4;
  }
  // simple parser
  triple->resize(0);
  mcpu->resize(0);
  mattr->resize(0);

  bool soft_float_abi = false;
  std::string key, value;
  std::istringstream is(target_str.substr(start, target_str.length() - start));

  while (is >> key) {
    if (key == "--system-lib" || key == "-system-lib") {
      continue;
    }
    size_t pos = key.find('=');
    if (pos != std::string::npos) {
      CHECK(key.length() > pos + 1)
          << "inavlid argument " << key << "\n";
      value = key.substr(pos + 1, key.length() - 1);
      key = key.substr(0, pos);
    } else {
      if(is >> value)
         LOG(FATAL) << "Unspecified value for option " << key;
    }
    if (key == "-target" ||
        key == "-mtriple") {
      *triple = value;
    } else if (key == "-mcpu") {
      *mcpu = value;
    } else if (key == "-mattr") {
      *mattr = value;
    } else if (key == "-mfloat-abi") {
      if (value == "hard") {
        soft_float_abi = false;
      } else if (value == "soft") {
        soft_float_abi = true;
      } else {
        LOG(FATAL) << "invalid -mfloat-abi option " << value;
      }
    } else if (key == "-device" || key == "-libs" || key == "-model") {
      // pass
    } else {
      LOG(FATAL) << "unknown option " << key;
    }
  }

  if (triple->length() == 0 ||
      *triple == "default") {
    *triple = llvm::sys::getDefaultTargetTriple();
  }
  // set target option
  llvm::TargetOptions& opt = *options;
  opt = llvm::TargetOptions();
  opt.AllowFPOpFusion = llvm::FPOpFusion::Fast;
  opt.UnsafeFPMath = false;
  opt.NoInfsFPMath = false;
  opt.NoNaNsFPMath = true;
  if (soft_float_abi) {
    opt.FloatABIType = llvm::FloatABI::Soft;
  } else {
    opt.FloatABIType = llvm::FloatABI::Hard;
  }
}


std::unique_ptr<llvm::TargetMachine>
GetLLVMTargetMachine(const std::string& target_str,
                     bool allow_null) {
  std::string target_triple, mcpu, mattr;
  llvm::TargetOptions opt;

  ParseLLVMTargetOptions(target_str,
                         &target_triple,
                         &mcpu,
                         &mattr,
                         &opt);

  if (target_triple.length() == 0 ||
      target_triple == "default") {
    target_triple = llvm::sys::getDefaultTargetTriple();
  }
  if (mcpu.length() == 0) {
    mcpu = "generic";
  }

  std::string err;
  const llvm::Target* target =
      llvm::TargetRegistry::lookupTarget(target_triple, err);
  if (target == nullptr) {
    CHECK(!allow_null) << err << " target_triple=" << target_triple;
    return nullptr;
  }
  llvm::TargetMachine* tm = target->createTargetMachine(
      target_triple, mcpu, mattr, opt, llvm::Reloc::PIC_);
  return std::unique_ptr<llvm::TargetMachine>(tm);
}
