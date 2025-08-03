#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <format>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>

#include <fmt/format.h>

#include "edn/edn.hpp"

namespace yeet
{
    

    class YeetCompileException : public std::exception {
    public:
        YeetCompileException(const edn::EdnNode& node, const std::string& msg, const std::string& filePath = "", const char* engineFile = nullptr, int engineLine = -1)
            : line(node.line), column(node.column), message(msg), filePath(filePath), nodeStr(edn::pprint(const_cast<edn::EdnNode&>(node), 0, false)), engineFile(engineFile ? engineFile : ""), engineLine(engineLine) {}

        const char* what() const noexcept override {
            static std::string formatted;
            #ifdef NDEBUG
            // Release: Only .yeet file info
            formatted = fmt::format("{}({},{}) : error: {}\nNode: {}", filePath, line, column, message, nodeStr);
            #else
            // Debug: Show both .yeet and engine file/line
            formatted = fmt::format("{}({},{}) : error: {}\nNode: {}\n[In Native Code: {}:{}]", filePath, line, column, message, nodeStr, engineFile, engineLine);
            #endif
            return formatted.c_str();
        }
    private:
        int line = -1;
        int column = 1;
        std::string message;
        std::string filePath;
        std::string nodeStr;
        std::string engineFile;
        int engineLine = -1;
    };

    class Engine
    {
    private:
        std::unique_ptr<llvm::orc::LLJIT> jit;
        std::unique_ptr<llvm::LLVMContext> context;
        std::unique_ptr<llvm::Module> mod;
        std::string filePath;

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
        Engine(const std::string& filePath);
        ~Engine();

        void run(std::string& s);

        const std::string& getFilePath() const { return filePath; }

    private:
        void initializeLLVM();
        llvm::Value* codegenInt(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenFloat(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenSymbol(const edn::EdnNode& node, llvm::IRBuilder<>& builder);
        llvm::Value* codegenList(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenCond(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssign(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignPointer(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignLiteral(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignStruct(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenAssignStructField(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenStructAccess(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenReference(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenDereference(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenBinop(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenWhile(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenDefn(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        llvm::Value* codegenCall(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder);
        
        void defineStructType(const edn::EdnNode& node, const std::vector<std::pair<std::string, std::string>>& fields, llvm::IRBuilder<>& builder, llvm::LLVMContext& context);
        // Set a struct field value (mutate in place)

    private:
        llvm::Type* getLLVMType(const edn::EdnNode& node, const std::string& typeStr, llvm::IRBuilder<>& builder);

        std::string dumpModule();
    };
}