#include "engine.hpp"

using namespace yeet;

#include "../edn/edn.hpp"

// Helper: Map type string to LLVM type
llvm::Type* getLLVMType(const std::string& typeStr, llvm::IRBuilder<>& builder) {
    if (typeStr == "int8") return builder.getInt8Ty();
    if (typeStr == "int16") return builder.getInt16Ty();
    if (typeStr == "int32") return builder.getInt32Ty();
    if (typeStr == "int64") return builder.getInt64Ty();
    if (typeStr == "float32") return llvm::Type::getFloatTy(builder.getContext());
    if (typeStr == "float64") return builder.getDoubleTy();
    if (typeStr == "void") return builder.getVoidTy();
    throw std::string("Unknown type string for LLVM type: ") + typeStr;
}



Engine::Engine() {
    initializeLLVM();
}

Engine::~Engine() {
    // Cleanup if necessary
}

void Engine::initializeLLVM()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    jit = std::move(*llvm::orc::LLJITBuilder().create());
    context = std::make_unique<llvm::LLVMContext>();
}


void Engine::run(std::string& s)
{
    llvmSymbolTable.clear();
    auto node = edn::read(s);
    auto module = std::make_unique<llvm::Module>("calc_module", *context);
    llvm::IRBuilder<> builder(*context);
    // Create function prototype: double calc()
    auto funcType = llvm::FunctionType::get(builder.getDoubleTy(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "calc", module.get());
    auto entry = llvm::BasicBlock::Create(*context, "entry", func);
    builder.SetInsertPoint(entry);
    llvm::Value* result = nullptr;
    try {
        result = this->codegenExpr(node, *context, builder);
    } catch (const char* msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    } catch (const std::string& msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    }
    // Only emit return if result is not nullptr (i.e., not a defn)
    if (result) {
        // If result is int, cast to double before returning
        if (result->getType()->isIntegerTy()) {
            result = builder.CreateSIToFP(result, builder.getDoubleTy(), "intToDouble");
        }
        builder.CreateRet(result);
    } else {
        // If top-level was a defn, try to call 'main' if it exists
        auto& moduleRef = *module;
        llvm::Function* mainFunc = moduleRef.getFunction("main");
        if (mainFunc) {
            llvm::IRBuilder<> callBuilder(entry);
            llvm::Value* callResult = callBuilder.CreateCall(mainFunc, {}, "callmain");
            callBuilder.CreateRet(callResult);
        } else {
            // No value to return, just return 0.0
            builder.CreateRet(llvm::ConstantFP::get(builder.getDoubleTy(), 0.0));
        }
    }

    // Print the generated LLVM IR
    std::cout << "\n===== Generated LLVM IR =====\n";
    module->print(llvm::outs(), nullptr);
    std::cout << "\n============================\n" << std::endl;

    // Add module to JIT
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        std::cerr << "Failed to add module to JIT: " << llvm::toString(std::move(err)) << std::endl;
        return;
    }
    // Look up and run the function
    auto sym = jit->lookup("calc");
    if (!sym) {
        std::cerr << "Failed to find function: " << llvm::toString(sym.takeError()) << std::endl;
        return;
    }
    try {
        auto calcFn = sym->toPtr<double(*)()>();
    
        double value = calcFn();
        std::cout << "JIT result: " << value << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error executing JIT function: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown error executing JIT function." << std::endl;
    }
    
}



// Helper for EdnInt
llvm::Value* Engine::codegenInt(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    if(node.metadata.count("type")) {
        std::string typeStr = node.metadata.at("type");
        llvm::Type* llvmType = getLLVMType(typeStr, builder);
        return llvm::ConstantInt::get(llvmType, std::stoi(node.value));
    }
    return llvm::ConstantInt::get(builder.getInt32Ty(), std::stoi(node.value));
}

// Helper for EdnFloat
llvm::Value* Engine::codegenFloat(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    if(node.metadata.count("type")) {
        std::string typeStr = node.metadata.at("type");
        if (typeStr == "float32") {
            return llvm::ConstantFP::get(builder.getFloatTy(), std::stof(node.value));
        } else if (typeStr == "float64") {
            return llvm::ConstantFP::get(builder.getDoubleTy(), std::stod(node.value));
        }
        throw std::string("Unknown float type: ") + typeStr;
    }
    return llvm::ConstantFP::get(builder.getDoubleTy(), std::stod(node.value));
}

// Helper for EdnSymbol
llvm::Value* Engine::codegenSymbol(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    if (node.value == "else") {
        return llvm::ConstantInt::get(builder.getInt32Ty(), 1);
    }
    auto it = llvmSymbolTable.find(node.value);
    if (it == llvmSymbolTable.end()) throw std::string("Unknown variable: ") + node.value;
    llvm::Value* alloca = it->second.first;
    std::string typeStr = it->second.second;
    llvm::Type* varType = getLLVMType(typeStr, builder);
    return builder.CreateLoad(varType, alloca, node.value);
}

llvm::Value* Engine::codegenAssign(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;


    // Assignment
    // Literal: (= target :type value)
    // Struct: (= target (StructName (Field1 Field2 ...)))
    // Struct Field Assignment: (= (. target :field) value)
    if (node.values.size() < 3) throw "Expected target and value";

    if(node.values.size() == 3) {
        auto it = node.values.begin();
        ++it; // Skip '='
        // Struct Construct
        if((*it).type == EdnSymbol) {
            return this->codegenAssignStruct(node, context, builder);
        }
        // Struct field assignment
        else if ((*it).type == EdnList) 
        {
            return this->codegenAssignStructField(node, context, builder);
        }
        
    }
    if(node.values.size() == 4) {
       return this->codegenAssignLiteral(node, context, builder);
    }
    
    throw "Assignment target must be a symbol or field access";
}

llvm::Value* Engine::codegenAssignStruct(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    //Struct: (= target (StructName (Field1 Field2 ...)))
    auto it = node.values.begin();
    ++it; // Skip '='

    const edn::EdnNode& targetNode = *it;
    if (targetNode.type != edn::EdnSymbol) throw "Expected Struct assignment target to be a symbol";
    ++it;

    auto structDeclarationNode = *it;
    if(structDeclarationNode.type != edn::EdnList || structDeclarationNode.values.size() < 2) {
        throw "Expected Struct assignment to be of form (StructName (Field1 Field2 ...))";
    }

    auto structIt = structDeclarationNode.values.begin();
    auto structNameNode = *structIt;
    if (structNameNode.type != edn::EdnSymbol) throw "Expected Struct name to be a symbol";
    if(llvmStructTypes.find(structNameNode.value) == llvmStructTypes.end()) {
        throw std::string("Struct type not defined: ") + structNameNode.value;
    }
    ++structIt; 
    auto fieldsNode = *structIt;
    if (fieldsNode.type != edn::EdnList) throw "Expected Struct fields";

    std::vector<llvm::Value*> fieldValues;
    for (const auto& fieldNode : fieldsNode.values) {  
        llvm::Value* fieldValue = this->codegenExpr(fieldNode, context, builder);
        fieldValues.push_back(fieldValue);
    }

    // Create struct instance with variable name and store pointer in symbol table
    llvm::Type* structType = llvmStructTypes.at(structNameNode.value);
    llvm::Value* structPtr = builder.CreateAlloca(structType, nullptr, targetNode.value);
    for (size_t i = 0; i < fieldValues.size(); ++i) {
        auto gep = builder.CreateStructGEP(structType, structPtr, i);
        builder.CreateStore(fieldValues[i], gep);
    }
    llvmSymbolTable[targetNode.value] = {structPtr, structNameNode.value};
    return structPtr;
}

llvm::Value* Engine::codegenAssignStructField(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {

    // 1) Struct Field Assignment: (= (. target :field) value)
    auto it = node.values.begin();
    ++it; // Skip '='

    // 2_ Extract target field access node
    const edn::EdnNode& targetFieldNode = *it;
    if (targetFieldNode.type != edn::EdnList || targetFieldNode.values.size() != 3) {
        throw "Expected Struct field assignment to be of form (= (. target :field) value)";
    }
    auto fieldAcessIt = targetFieldNode.values.begin();
    auto dot = *fieldAcessIt;
    if (dot.type != edn::EdnSymbol || dot.value != ".") {
        throw "Expected Struct field access to start with '.'";
    }
    ++fieldAcessIt; // Skip '.'
    const edn::EdnNode& structTargetNode = *fieldAcessIt;
    if (structTargetNode.type != edn::EdnSymbol) {
        throw "Expected Struct field access target to be a symbol";
    }
    ++fieldAcessIt; 
    const edn::EdnNode& fieldNode = *fieldAcessIt;
    if (fieldNode.type != edn::EdnKeyword) {
        throw "Expected Struct field to be a keyword";
    }

    auto symbolIt = llvmSymbolTable.find(structTargetNode.value);
    if (symbolIt == llvmSymbolTable.end()) {
        throw std::string("Struct target not defined: ") + structTargetNode.value;
    }

    std::string structName = symbolIt->second.second;
    std::string fieldName = fieldNode.value.substr(1); // Remove leading ':'

    auto yeetStructValueIt = yeetStructTable.find(structName);
    if (yeetStructValueIt == yeetStructTable.end()) {
        throw std::string("Struct not defined: ") + structName;
    }
    auto& yeetStructType = yeetStructValueIt->second;
    

    // Test if variable is a pointer to a struct
    auto structTypePointer = symbolIt->second.first;
    

    // lookup llvm struct type definition
    auto llvmStructTypeDefIt = llvmStructTypes.find(structName);
    if (llvmStructTypeDefIt == llvmStructTypes.end()) {
        throw std::string("Struct type not defined: ") + structName;
    }
    auto llvmStructTypeDef = llvmStructTypeDefIt->second;

    // Lookup field index in struct type
    auto fieldIndexIt = std::find_if(yeetStructType.begin(), yeetStructType.end(),
        [&fieldName](const auto& field) { return field.first == fieldName; });
    

    if (fieldIndexIt == yeetStructType.end()) {
        throw std::string("Field not a member of struct") + fieldName + " in struct " + structName;;
    }
    auto fieldIndexId = std::distance(yeetStructType.begin(), fieldIndexIt);
    auto fieldType = fieldIndexIt->second;
    

    // 3_ extract value node
    ++it; // Move to value node
    const edn::EdnNode& valueNode = *it;
    llvm::Value* value = this->codegenExpr(valueNode, context, builder);
    if (value->getType() != getLLVMType(fieldType, builder)) {
        throw std::string("Value type mismatch for field: ") + fieldName;
    }

    auto gep = builder.CreateStructGEP(llvmStructTypeDef, structTypePointer,  fieldIndexId);
    return builder.CreateStore(value, gep);
}

llvm::Value* Engine::codegenAssignLiteral(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {

    // 1) Exract variable node
    auto it = node.values.begin();
    ++it; // targetNode
    const edn::EdnNode& variableNode = *it;
    if (variableNode.type != edn::EdnSymbol) throw "Expected variable symbol";
    ++it;

    // 2) Extract type Node
    const edn::EdnNode& typeNode = *it;
    if (typeNode.type != edn::EdnKeyword) throw "Expected type keyword";
    std::string typeStr = typeNode.value.substr(1); // remove leading ':'
    ++it; // valueNode

    edn::EdnNode valueNode = *it;

    llvm::Value* value = nullptr;
    llvm::Value* _alloca = nullptr;

    // Handle codegen for valueNode
    if (valueNode.type == edn::EdnInt || valueNode.type == edn::EdnFloat) {
        valueNode.metadata["type"] = typeStr;
        value = this->codegenExpr(valueNode, context, builder);
    }
    else if(valueNode.type == edn::EdnSymbol) {
        // If it's a symbol, just load the value
        value = this->codegenSymbol(valueNode, builder);
    }
    else if(valueNode.type == edn::EdnList) {
        // If it's a list, treat it as a struct or function call
        value = this->codegenList(valueNode, context, builder);
    }
    else {
        throw "Expected value to be an int, float, or symbol, or list.";
    }

    // 4 Create alloca for the variable
    llvm::Type* llvmType = getLLVMType(typeStr, builder);
    auto symIt = llvmSymbolTable.find(variableNode.value);
    if (symIt == llvmSymbolTable.end()) {
        _alloca = builder.CreateAlloca(llvmType, nullptr, variableNode.value);
        llvmSymbolTable[variableNode.value] = std::make_pair(_alloca, typeStr);
    } else {
        _alloca = symIt->second.first;
    }
    builder.CreateStore(value, _alloca);

    return value;
}

// Helper for EdnList
llvm::Value* Engine::codegenList(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // If this is a sequence of expressions (not an operation), evaluate each and return the last
    bool allAreLists = true;
    for (const auto& v : node.values) {
        if (v.type != EdnList && v.type != EdnInt && v.type != EdnSymbol && v.type != EdnFloat) {
            allAreLists = false;
            break;
        }
    }
    if (allAreLists && node.values.size() > 1 && node.values.front().type == EdnList) {
        llvm::Value* last = nullptr;
        for (const auto& expr : node.values) {
            last = this->codegenExpr(expr, context, builder);
        }
        return last;
    }
    // Otherwise, treat as operation
    const EdnNode& opNode = node.values.front();
    if (opNode.type != EdnSymbol) throw "Expected operator symbol";
    std::string op = opNode.value;
    if (op == ".") {
        return this->codegenStructAccess(node, context, builder);
    }
    if (op == "defn") {
        return this->codegenDefn(node, context, builder);
    }
    if (op == "cond") {
        return this->codegenCond(node, context, builder);
    }
    if (op == "=") {
        return this->codegenAssign(node, context, builder);
    }
    if (op == "while") {
        return this->codegenWhile(node, context, builder);
    }
    if (op == "struct") {
        // (struct name ((field1 :type1) (field2 :type2) ...))
        if (node.values.size() != 3) throw "struct requires a name and a field list";
        auto it = node.values.begin();
        ++it; // nameNode
        const edn::EdnNode& nameNode = *it;
        ++it; // fieldsNode
        const edn::EdnNode& fieldsNode = *it;
        if (nameNode.type != edn::EdnSymbol) throw "struct: name must be a symbol";
        if (fieldsNode.type != edn::EdnList) throw "struct: fields must be a list";
        std::vector<std::pair<std::string, std::string>> fields;
        for (const auto& field : fieldsNode.values) {
            if (field.type == edn::EdnList && field.values.size() == 2 && field.values.front().type == edn::EdnSymbol && field.values.back().type == edn::EdnKeyword) {
                fields.push_back({field.values.front().value, field.values.back().value.substr(1)});
            } else {
                throw "struct: each field must be (name :type)";
            }
        }
        this->defineStructType(nameNode.value, fields, context);
        return nullptr; // struct definition does not produce a value
    }
    if(op == "+" || op == "-" || op == "*" || op == "/" || op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        return this->codegenBinop(node, context, builder);
    }
    // Function call: (name arg1 arg2 ...)
    if (opNode.type == edn::EdnSymbol && yeetFunctionTable.count(op) > 0) {
        return this->codegenCall(node, context, builder);
    }
    throw std::string("Unknown operator: ") + op;
}

// (defn name (args...) body...)
llvm::Value* Engine::codegenDefn(const edn::EdnNode& node, llvm::LLVMContext&, llvm::IRBuilder<>&) {
    using namespace edn;
    if (node.values.size() < 5) throw "defn requires a return type, name, arg list, and body";
    const EdnNode& retTypeNode = *(++node.values.begin());
    const EdnNode& nameNode = *(++++node.values.begin());
    const EdnNode& argsNode = *(++++++node.values.begin());
    if (retTypeNode.type != EdnKeyword) throw "defn: first argument must be return type keyword";
    if (nameNode.type != EdnSymbol) throw "defn: function name must be a symbol";
    if (argsNode.type != EdnList) throw "defn: argument list must be a list";
    std::string retType = retTypeNode.value.substr(1); // remove leading ':'
    std::vector<std::pair<std::string, std::string>> args;
    for (const auto& arg : argsNode.values) {
        if (arg.type == EdnList && arg.values.size() == 2 && arg.values.front().type == EdnSymbol && arg.values.back().type == EdnKeyword) {
            args.push_back({arg.values.front().value, arg.values.back().value.substr(1)});
        } else if (arg.type == EdnSymbol) {
            args.push_back({arg.value, "int32"});
        } else {
            throw "defn: all arguments must be symbols or (name :type)";
        }
    }
    EdnNode bodyNode = node;
    bodyNode.values.erase(bodyNode.values.begin(), std::next(bodyNode.values.begin(), 4));
    yeetFunctionTable[nameNode.value] = {args, bodyNode};
    yeetFunctionReturnTypes[nameNode.value] = retType;
    return nullptr; // defn does not produce a value
}

// (name arg1 arg2 ...)
llvm::Value* Engine::codegenCall(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    const EdnNode& opNode = node.values.front();
    auto it = yeetFunctionTable.find(opNode.value);
    if (it == yeetFunctionTable.end()) throw std::string("Unknown function: ") + opNode.value;
    const auto& [args, bodyNode] = it->second;
    if (node.values.size() - 1 != args.size()) throw "Function argument count mismatch";
    // Get return type
    std::string retType = "double";
    auto retIt = yeetFunctionReturnTypes.find(opNode.value);
    if (retIt != yeetFunctionReturnTypes.end()) retType = retIt->second;
    llvm::Type* llvmRetType = getLLVMType(retType, builder);
    // Create function type
    std::vector<llvm::Type*> argTypes;
    for (const auto& arg : args) {
        argTypes.push_back(getLLVMType(arg.second, builder));
    }
    auto funcType = llvm::FunctionType::get(llvmRetType, argTypes, false);
    auto& module = *builder.GetInsertBlock()->getModule();
    llvm::Function* func = module.getFunction(opNode.value);
    if (!func) {
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, opNode.value, &module);
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", func);
        llvm::IRBuilder<> funcBuilder(entry);
        auto argIt = func->arg_begin();
        for (size_t i = 0; i < args.size(); ++i, ++argIt) {
            llvm::Type* argType = argTypes[i];
            llvm::Value* alloca = funcBuilder.CreateAlloca(argType, nullptr, args[i].first);
            funcBuilder.CreateStore(&*argIt, alloca);
            llvmSymbolTable[args[i].first] = std::make_pair(alloca, args[i].second);
        }
        llvm::Value* result = nullptr;
        for (const auto& expr : bodyNode.values) {
            result = this->codegenExpr(expr, context, funcBuilder);
        }
        if (retType != "void") {
            // If result type doesn't match return type, cast
            if (result && result->getType() != llvmRetType) {
                if (llvmRetType->isFloatingPointTy() && result->getType()->isIntegerTy()) {
                    result = funcBuilder.CreateSIToFP(result, llvmRetType, "intToFloatRet");
                } else if (llvmRetType->isIntegerTy() && result->getType()->isFloatingPointTy()) {
                    result = funcBuilder.CreateFPToSI(result, llvmRetType, "floatToIntRet");
                } else if (llvmRetType->isIntegerTy() && result->getType()->isIntegerTy() && llvmRetType != result->getType()) {
                    result = funcBuilder.CreateIntCast(result, llvmRetType, true, "intCastRet");
                }
            }
            funcBuilder.CreateRet(result);
        } else {
            funcBuilder.CreateRetVoid();
        }
    }
    std::vector<llvm::Value*> callArgs;
    auto argNodeIt = std::next(node.values.begin());
    for (size_t i = 0; i < args.size(); ++i, ++argNodeIt) {
        llvm::Value* argVal = this->codegenExpr(*argNodeIt, context, builder);
        llvm::Type* expectedType = argTypes[i];
        llvm::Type* actualType = argVal->getType();
        if (expectedType != actualType) {
            if (expectedType->isFloatingPointTy() && actualType->isIntegerTy()) {
                argVal = builder.CreateSIToFP(argVal, expectedType, "intToFloatArg");
            } else if (expectedType->isIntegerTy() && actualType->isFloatingPointTy()) {
                argVal = builder.CreateFPToSI(argVal, expectedType, "floatToIntArg");
            } else if (expectedType->isIntegerTy() && actualType->isIntegerTy() && expectedType != actualType) {
                argVal = builder.CreateIntCast(argVal, expectedType, true, "intCastArg");
            }
        }
        callArgs.push_back(argVal);
    }
    return builder.CreateCall(func, callArgs, "calltmp");
}

llvm::Value* Engine::codegenWhile(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // (while test body)
    if (node.values.size() != 3) throw "while requires a test and a body";
    const EdnNode& testNode = *(++node.values.begin());
    const EdnNode& bodyNode = *(++++node.values.begin());
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "while.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(context, "while.after", function);
    builder.CreateBr(condBB);
    // Condition block
    builder.SetInsertPoint(condBB);
    llvm::Value* condVal = this->codegenExpr(testNode, context, builder);
    if (condVal->getType()->isFloatingPointTy()) {
        condVal = builder.CreateFCmpONE(condVal, llvm::ConstantFP::get(condVal->getType(), 0.0), "whilecond");
    } else {
        condVal = builder.CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "whilecond");
    }
    builder.CreateCondBr(condVal, bodyBB, afterBB);
    // Body block
    builder.SetInsertPoint(bodyBB);
    this->codegenExpr(bodyNode, context, builder);
    builder.CreateBr(condBB);
    // After block
    builder.SetInsertPoint(afterBB);
    // Return 0.0 as the value of the while loop (could be changed to last body value if desired)
    return llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
}


// Main dispatcher
llvm::Value* Engine::codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    switch (node.type) {
        case EdnInt:
            return codegenInt(node, builder);
        case EdnFloat:
            return codegenFloat(node, builder);
        case EdnSymbol:
            return codegenSymbol(node, builder);
        case EdnList:
            return codegenList(node, context, builder);
        default:
            throw "Unsupported expression";
    }
}

// Helper for cond special form
llvm::Value* Engine::codegenCond(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // (cond (test1 expr1) (test2 expr2) ... (else exprN))
    if (node.values.size() < 2) throw "cond requires at least one clause";
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(context, "cond.after", function);
    llvm::PHINode* phi = nullptr;
    // Only create and fill reachable clause blocks
    std::vector<std::pair<const edn::EdnNode*, llvm::BasicBlock*>> clauses;
    llvm::BasicBlock* dispatchBB = builder.GetInsertBlock();
    size_t lastDispatched = 0;
    auto it = std::next(node.values.begin());
    for (; it != node.values.end(); ++it) {
        clauses.emplace_back(&(*it), llvm::BasicBlock::Create(context, "cond.clause", function));
        const edn::EdnNode* clause = &(*it);
        auto clauseIt = clause->values.begin();
        const edn::EdnNode* testNode = nullptr;
        if (clause->values.size() == 2) {
            testNode = &(*clauseIt);
        }
        builder.SetInsertPoint(dispatchBB);
        llvm::Value* testVal = nullptr;
        if (clause->values.size() == 2) {
            testVal = this->codegenExpr(*testNode, context, builder);
        }
        if (clause->values.size() == 1 || (testNode && testNode->type == edn::EdnSymbol && testNode->value == "else") || std::next(it) == node.values.end()) {
            builder.CreateBr(clauses.back().second);
            lastDispatched = clauses.size();
            ++it;
            break;
        } else {
            llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(context, "cond.clause", function);
            builder.CreateCondBr(testVal, clauses.back().second, nextBB);
            dispatchBB = nextBB;
            lastDispatched = clauses.size();
        }
    }
    // Now fill in each clause block (only up to lastDispatched)
    phi = llvm::PHINode::Create(builder.getDoubleTy(), lastDispatched, "condresult", afterBB);
    for (size_t i = 0; i < lastDispatched; ++i) {
        llvm::BasicBlock* clauseBB = clauses[i].second;
        builder.SetInsertPoint(clauseBB);
        const edn::EdnNode* clause = clauses[i].first;
        auto exprIt = clause->values.end();
        --exprIt;
        const edn::EdnNode& exprNode = *exprIt;
        llvm::Value* exprVal = this->codegenExpr(exprNode, context, builder);
        llvm::Value* castVal = exprVal;
        if (exprVal->getType()->isIntegerTy()) {
            castVal = builder.CreateSIToFP(exprVal, builder.getDoubleTy(), "intToDouble");
        } else if (exprVal->getType()->isFloatTy() && !exprVal->getType()->isDoubleTy()) {
            castVal = builder.CreateFPExt(exprVal, builder.getDoubleTy(), "floatToDouble");
        }
        builder.CreateBr(afterBB);
        phi->addIncoming(castVal, clauseBB);
    }
    builder.SetInsertPoint(afterBB);
    return phi;
}

llvm::Value* Engine::codegenBinop(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    using namespace edn;
    const EdnNode& opNode = node.values.front();
    std::string op = opNode.value;
    if (node.values.size() != 3) throw "Expected two operands";
    auto lhsIt = ++node.values.begin();
    auto rhsIt = ++++node.values.begin();
    llvm::Value* lhs = this->codegenExpr(*lhsIt, context, builder);
    llvm::Value* rhs = this->codegenExpr(*rhsIt, context, builder);
    // Use type info from symbolTable if available
    std::string lhsType = "int32";
    std::string rhsType = "int32";
    if (lhsIt->type == EdnSymbol) {
        auto it = llvmSymbolTable.find(lhsIt->value);
        if (it != llvmSymbolTable.end()) lhsType = it->second.second;
    } else if (lhsIt->type == EdnFloat) {
        lhsType = "float64";
    }
    if (rhsIt->type == EdnSymbol) {
        auto it = llvmSymbolTable.find(rhsIt->value);
        if (it != llvmSymbolTable.end()) rhsType = it->second.second;
    } else if (rhsIt->type == EdnFloat) {
        rhsType = "float64";
    }

    llvm::Type* lhsLLVMType = getLLVMType(lhsType, builder);
    llvm::Type* rhsLLVMType = getLLVMType(rhsType, builder);

    // Promote types for binops: if either is float, promote both to float64; else promote to largest int
    bool lhsIsFloat = lhsLLVMType->isFloatingPointTy();
    bool rhsIsFloat = rhsLLVMType->isFloatingPointTy();
    bool isFloatOp = lhsIsFloat || rhsIsFloat;

    // For integer binops, promote to largest bitwidth
    unsigned intBitwidth = std::max(lhsLLVMType->getIntegerBitWidth(), rhsLLVMType->getIntegerBitWidth());
    llvm::Type* promotedIntType = nullptr;
    if (!isFloatOp) {
        if (intBitwidth == 8) promotedIntType = builder.getInt8Ty();
        else if (intBitwidth == 16) promotedIntType = builder.getInt16Ty();
        else if (intBitwidth == 32) promotedIntType = builder.getInt32Ty();
        else if (intBitwidth == 64) promotedIntType = builder.getInt64Ty();
        else throw std::string("Unsupported integer bitwidth: ") + std::to_string(intBitwidth);
        if (lhsLLVMType != promotedIntType) lhs = builder.CreateIntCast(lhs, promotedIntType, true, "intCastL");
        if (rhsLLVMType != promotedIntType) rhs = builder.CreateIntCast(rhs, promotedIntType, true, "intCastR");
    } else {
        // Promote both to float64 for now
        if (!lhsIsFloat) lhs = builder.CreateSIToFP(lhs, builder.getDoubleTy(), "intToDoubleL");
        if (!rhsIsFloat) rhs = builder.CreateSIToFP(rhs, builder.getDoubleTy(), "intToDoubleR");
        lhsLLVMType = builder.getDoubleTy();
        rhsLLVMType = builder.getDoubleTy();
    }

    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (isFloatOp) {
            if (op == "==") return builder.CreateUIToFP(builder.CreateFCmpUEQ(lhs, rhs, "cmptmp"), builder.getDoubleTy());
            if (op == "!=") return builder.CreateUIToFP(builder.CreateFCmpUNE(lhs, rhs, "cmptmp"), builder.getDoubleTy());
            if (op == "<")  return builder.CreateUIToFP(builder.CreateFCmpULT(lhs, rhs, "cmptmp"), builder.getDoubleTy());
            if (op == "<=") return builder.CreateUIToFP(builder.CreateFCmpULE(lhs, rhs, "cmptmp"), builder.getDoubleTy());
            if (op == ">")  return builder.CreateUIToFP(builder.CreateFCmpUGT(lhs, rhs, "cmptmp"), builder.getDoubleTy());
            if (op == ">=") return builder.CreateUIToFP(builder.CreateFCmpUGE(lhs, rhs, "cmptmp"), builder.getDoubleTy());
        } else {
            if (op == "==") return builder.CreateICmpEQ(lhs, rhs, "cmptmp");
            if (op == "!=") return builder.CreateICmpNE(lhs, rhs, "cmptmp");
            if (op == "<")  return builder.CreateICmpSLT(lhs, rhs, "cmptmp");
            if (op == "<=") return builder.CreateICmpSLE(lhs, rhs, "cmptmp");
            if (op == ">")  return builder.CreateICmpSGT(lhs, rhs, "cmptmp");
            if (op == ">=") return builder.CreateICmpSGE(lhs, rhs, "cmptmp");
        }
    }
    if (isFloatOp) {
        if (op == "+") return builder.CreateFAdd(lhs, rhs, "faddtmp");
        if (op == "-") return builder.CreateFSub(lhs, rhs, "fsubtmp");
        if (op == "*") return builder.CreateFMul(lhs, rhs, "fmultmp");
        if (op == "/") return builder.CreateFDiv(lhs, rhs, "fdivtmp");
    } else {
        if (op == "+") return builder.CreateAdd(lhs, rhs, "addtmp");
        if (op == "-") return builder.CreateSub(lhs, rhs, "subtmp");
        if (op == "*") return builder.CreateMul(lhs, rhs, "multmp");
        if (op == "/") return builder.CreateSDiv(lhs, rhs, "divtmp");
    }
    throw std::string("Unknown operator: ") + op;
}



// Helper: Define a struct type
void Engine::defineStructType(const std::string& name, const std::vector<std::pair<std::string, std::string>>& fields, llvm::LLVMContext& context) {
    // 1 Check if struct type already exists
    if (llvmStructTypes.find(name) != llvmStructTypes.end()) {  
        throw std::string("Struct type already defined: ") + name;
    }
    yeetStructTable[name] = fields;

    // 2 Build LLVM representation
    std::vector<llvm::Type*> llvmFields;
    for (const auto& [fieldName, fieldType] : fields) {
        llvmFields.push_back(getLLVMType(fieldType, *(new llvm::IRBuilder<>(context))));
    }
    auto structType = llvm::StructType::create(context, llvmFields, name);
    llvmStructTypes[name] = structType;
}

llvm::Value* Engine::codegenStructAccess(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    // Expect: (. target :field)
    if (node.values.size() != 3)
        throw "Struct field access must be of form (. target :field)";

    auto it = node.values.begin();
    auto dotNode = *it;
    if (dotNode.type != edn::EdnSymbol || dotNode.value != ".")
        throw "Struct field access must start with '.'";
    ++it;
    const edn::EdnNode& structTargetNode = *it;
    if (structTargetNode.type != edn::EdnSymbol)
        throw "Struct field access target must be a symbol";
    ++it;
    const edn::EdnNode& fieldNode = *it;
    if (fieldNode.type != edn::EdnKeyword)
        throw "Struct field must be a keyword";

    auto symbolIt = llvmSymbolTable.find(structTargetNode.value);
    if (symbolIt == llvmSymbolTable.end())
        throw std::string("Struct target not defined: ") + structTargetNode.value;

    std::string structName = symbolIt->second.second;
    std::string fieldName = fieldNode.value.substr(1); // Remove leading ':'

    auto yeetStructValueIt = yeetStructTable.find(structName);
    if (yeetStructValueIt == yeetStructTable.end())
        throw std::string("Struct not defined: ") + structName;
    auto& yeetStructType = yeetStructValueIt->second;

    // Type check: must be pointer to struct
    auto structTypePointer = symbolIt->second.first;
    
    
    // lookup llvm struct type definition
    auto llvmStructTypeDefIt = llvmStructTypes.find(structName);
    if (llvmStructTypeDefIt == llvmStructTypes.end())
        throw std::string("Struct type not defined: ") + structName;
    auto llvmStructTypeDef = llvmStructTypeDefIt->second;

    // Lookup field index in struct type
    auto fieldIndexIt = std::find_if(yeetStructType.begin(), yeetStructType.end(),
        [&fieldName](const auto& field) { return field.first == fieldName; });
    if (fieldIndexIt == yeetStructType.end())
        throw std::string("Field not a member of struct: ") + fieldName + " in struct " + structName;
    auto fieldIndexId = std::distance(yeetStructType.begin(), fieldIndexIt);
    auto fieldType = fieldIndexIt->second;

    // Access field value
    auto gep = builder.CreateStructGEP(llvmStructTypeDef, structTypePointer, fieldIndexId);
    return builder.CreateLoad(getLLVMType(fieldType, builder), gep, fieldName);
}


