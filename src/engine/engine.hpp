#pragma once
#include <string>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>

#include "edn/edn.hpp"

namespace yeet
{
    class Engine
    {
    private:
        std::unordered_map<std::string, llvm::Value*> symbolTable;
        std::unique_ptr<llvm::orc::LLJIT> jit;
        std::unique_ptr<llvm::LLVMContext> context;
    public:
        Engine();
        ~Engine();

        void run(std::string& s);

    private:
        void initializeLLVM();
        llvm::Value* codegenInt(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenFloat(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenSymbol(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenList(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        
        // Add any other necessary member variables or methods here
    };
}