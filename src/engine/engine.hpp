#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>

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
        std::unique_ptr<llvm::orc::LLJIT> jit;
        std::unique_ptr<llvm::LLVMContext> context;

    private:
        // LLVM Variable/Struct definitions
        // Symbol table: name -> (alloca, type string)
        std::unordered_map<std::string, std::pair<llvm::Value*, std::string>> llvmSymbolTable;
        std::map<std::string, llvm::StructType*> llvmStructTypes;
        
    private:
        // Structure Type Definitions
        std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> yeetStructTable;
        // Function table for lazy generation / TODO generic function handling
        std::unordered_map<std::string, std::pair<std::vector<std::pair<std::string, std::string>>, edn::EdnNode>> yeetFunctionTable;
        // Function return types: name -> type string
        std::unordered_map<std::string, std::string> yeetFunctionReturnTypes;
        
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
        llvm::Value* codegenAssignLiteral(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignStruct(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignStructField(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenStructAccess(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenBinop(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenWhile(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenDefn(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenCall(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        
        
        void defineStructType(const std::string& name, const std::vector<std::pair<std::string, std::string>>& fields, llvm::LLVMContext& context);
        // Set a struct field value (mutate in place)
        
        
        // Add any other necessary member variables or methods here
    };
}