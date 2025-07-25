#pragma once
#include <string>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>


namespace yeet
{
    class Engine
    {
    private:
        std::unique_ptr<llvm::orc::LLJIT> _jit;
        std::unique_ptr<llvm::LLVMContext> _context;
    public:
        Engine();
        ~Engine();

        void run(std::string& s);

    private:
        void initializeLLVM();

        // Add any other necessary member variables or methods here
    };
}