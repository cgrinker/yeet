#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

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
        // Symbol table: name -> (alloca, type string)
        std::unordered_map<std::string, std::pair<llvm::Value*, std::string>> symbolTable;
        std::unique_ptr<llvm::orc::LLJIT> jit;
        std::unique_ptr<llvm::LLVMContext> context;
        
        // Function table: name -> (args/types, body)
        std::unordered_map<std::string, std::pair<std::vector<std::pair<std::string, std::string>>, edn::EdnNode>> functionTable;
        // Function return types: name -> type string
        std::unordered_map<std::string, std::string> functionReturnTypes;
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
        llvm::Value* codegenCond(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssign(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenBinop(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenWhile(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenDefn(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenCall(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        
        // Add any other necessary member variables or methods here
    };
}