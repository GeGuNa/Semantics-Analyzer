#include <map>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

enum class SymbolType {
    Variable,
    Constant,
    Function
};

enum class TypeKind {
    Int, Float, String, Bool, Auto, Unknown
};

struct TypeInfo {
    TypeKind kind;
    int width; // For numeric types (8, 16, 32, etc.)
    bool is_signed;
    bool is_mutable;
    
    bool operator==(const TypeInfo& other) const {
        return kind == other.kind && 
               width == other.width && 
               is_signed == other.is_signed;
    }
};

struct Symbol {
    std::string name;
    SymbolType symbol_type;
    TypeInfo type_info;
    bool is_initialized;
    int declaration_line;
};

class SemanticError : public std::runtime_error {
public:
    int line;
    SemanticError(const std::string& msg, int line) 
        : std::runtime_error(msg), line(line) {}
};

class SemanticAnalyzer {
    std::map<std::string, Symbol> symbol_table;
    std::vector<std::map<std::string, Symbol>> scope_stack;
    TypeInfo current_return_type;
    bool in_function = false;
    
public:
    SemanticAnalyzer() {
        // Push global scope
        enter_scope();
        
        // Add built-in types
        // Could add more here as needed
    }
    
    void analyze(std::shared_ptr<ASTNode> root) {
        visit(root);
    }
    
private:
    void enter_scope() {
        scope_stack.emplace_back();
    }
    
    void exit_scope() {
        if (scope_stack.empty()) {
            throw std::logic_error("Scope stack underflow");
        }
        scope_stack.pop_back();
    }
    
    Symbol* find_symbol(const std::string& name) {
        // Check scopes from innermost to outermost
        for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }
    
    void visit(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        switch (node->kind) {
            case NodeKind::Function: {
                visit_function(std::static_pointer_cast<FunctionNode>(node));
                break;
            }
            case NodeKind::LetDeclaration: {
                visit_let_decl(std::static_pointer_cast<LetDeclarationNode>(node));
                break;
            }
            case NodeKind::VarDeclaration: {
                visit_var_decl(std::static_pointer_cast<VarDeclarationNode>(node));
                break;
            }
            case NodeKind::ConstDeclaration: {
                visit_const_decl(std::static_pointer_cast<ConstDeclarationNode>(node));
                break;
            }
            // Handle other node types...
        }
    }
    
    void visit_function(std::shared_ptr<FunctionNode> func) {
        // Check for duplicate function name
        if (find_symbol(func->name)) {
            throw SemanticError("Duplicate function name '" + func->name + "'", func->line);
        }
        
        // Add function to symbol table
        Symbol func_sym;
        func_sym.name = func->name;
        func_sym.symbol_type = SymbolType::Function;
        func_sym.type_info = parse_type(func->return_type);
        func_sym.is_initialized = true;
        func_sym.declaration_line = func->line;
        scope_stack.back()[func->name] = func_sym;
        
        // Process function body
        in_function = true;
        current_return_type = func_sym.type_info;
        enter_scope();
        
        // Add parameters to scope
        for (const auto& param : func->parameters) {
            visit_parameter(param);
        }
        
        // Visit all statements in function body
        for (const auto& stmt : func->body) {
            visit(stmt);
        }
        
        exit_scope();
        in_function = false;
    }
    
    void visit_parameter(std::shared_ptr<ParameterNode> param) {
        Symbol sym;
        sym.name = param->name;
        sym.symbol_type = SymbolType::Variable;
        sym.type_info = parse_type(param->type);
        sym.is_initialized = true; // Parameters are always initialized
        sym.declaration_line = param->line;
        
        // Check for duplicate parameter name
        if (scope_stack.back().count(param->name)) {
            throw SemanticError("Duplicate parameter name '" + param->name + "'", param->line);
        }
        
        scope_stack.back()[param->name] = sym;
    }
    
    void visit_let_decl(std::shared_ptr<LetDeclarationNode> let_decl) {
        Symbol sym;
        sym.name = let_decl->name;
        sym.symbol_type = SymbolType::Variable;
        sym.declaration_line = let_decl->line;
        
        // Check for duplicate name in current scope
        if (scope_stack.back().count(let_decl->name)) {
            throw SemanticError("Duplicate variable name '" + let_decl->name + "'", let_decl->line);
        }
        
        // Handle type annotation
        if (let_decl->type_annotation) {
            sym.type_info = parse_type(let_decl->type_annotation);
        } else {
            sym.type_info.kind = TypeKind::Auto;
        }
        
        // Handle initialization
        if (let_decl->initializer) {
            TypeInfo init_type = visit_expression(let_decl->initializer);
            
            if (sym.type_info.kind == TypeKind::Auto) {
                // Type inference
                sym.type_info = init_type;
            } else {
                // Check type compatibility
                if (!types_compatible(sym.type_info, init_type)) {
                    throw SemanticError("Type mismatch in let declaration", let_decl->line);
                }
            }
            
            sym.is_initialized = true;
        } else {
            sym.is_initialized = false;
        }
        
        // Immutable by default for let
        sym.type_info.is_mutable = false;
        
        scope_stack.back()[let_decl->name] = sym;
    }
    
    void visit_var_decl(std::shared_ptr<VarDeclarationNode> var_decl) {
        Symbol sym;
        sym.name = var_decl->name;
        sym.symbol_type = SymbolType::Variable;
        sym.declaration_line = var_decl->line;
        
        // Check for duplicate name in current scope
        if (scope_stack.back().count(var_decl->name)) {
            throw SemanticError("Duplicate variable name '" + var_decl->name + "'", var_decl->line);
        }
        
        // Handle type annotation
        if (var_decl->type_annotation) {
            sym.type_info = parse_type(var_decl->type_annotation);
        } else {
            sym.type_info.kind = TypeKind::Auto;
        }
        
        // var declarations must have initializers
        if (!var_decl->initializer) {
            throw SemanticError("var declaration requires initializer", var_decl->line);
        }
        
        TypeInfo init_type = visit_expression(var_decl->initializer);
        
        if (sym.type_info.kind == TypeKind::Auto) {
            // Type inference
            sym.type_info = init_type;
        } else {
            // Check type compatibility
            if (!types_compatible(sym.type_info, init_type)) {
                throw SemanticError("Type mismatch in var declaration", var_decl->line);
            }
        }
        
        sym.is_initialized = true;
        sym.type_info.is_mutable = true; // var is mutable
        
        scope_stack.back()[var_decl->name] = sym;
    }
    
    void visit_const_decl(std::shared_ptr<ConstDeclarationNode> const_decl) {
        Symbol sym;
        sym.name = const_decl->name;
        sym.symbol_type = SymbolType::Constant;
        sym.declaration_line = const_decl->line;
        
        // Check for duplicate name in current scope
        if (scope_stack.back().count(const_decl->name)) {
            throw SemanticError("Duplicate constant name '" + const_decl->name + "'", const_decl->line);
        }
        
        // Handle type annotation
        if (const_decl->type_annotation) {
            sym.type_info = parse_type(const_decl->type_annotation);
        } else {
            sym.type_info.kind = TypeKind::Auto;
        }
        
        // const declarations must have initializers
        if (!const_decl->initializer) {
            throw SemanticError("const declaration requires initializer", const_decl->line);
        }
        
        TypeInfo init_type = visit_expression(const_decl->initializer);
        
        if (sym.type_info.kind == TypeKind::Auto) {
            // Type inference
            sym.type_info = init_type;
        } else {
            // Check type compatibility
            if (!types_compatible(sym.type_info, init_type)) {
                throw SemanticError("Type mismatch in const declaration", const_decl->line);
            }
        }
        
        sym.is_initialized = true;
        sym.type_info.is_mutable = false; // const is immutable
        
        scope_stack.back()[const_decl->name] = sym;
    }
    
    TypeInfo visit_expression(std::shared_ptr<ExpressionNode> expr) {
        // This would evaluate the expression and return its type
        // Implementation depends on your expression hierarchy
        return TypeInfo{TypeKind::Unknown, 0, false, false};
    }
    
    TypeInfo parse_type(const std::string& type_name) {
        if (type_name == "i8") return {TypeKind::Int, 8, true, false};
        if (type_name == "u8") return {TypeKind::Int, 8, false, false};
        if (type_name == "i16") return {TypeKind::Int, 16, true, false};
        if (type_name == "u16") return {TypeKind::Int, 16, false, false};
        if (type_name == "i32") return {TypeKind::Int, 32, true, false};
        if (type_name == "u32") return {TypeKind::Int, 32, false, false};
        if (type_name == "i64") return {TypeKind::Int, 64, true, false};
        if (type_name == "u64") return {TypeKind::Int, 64, false, false};
        if (type_name == "i128") return {TypeKind::Int, 128, true, false};
        if (type_name == "u128") return {TypeKind::Int, 128, false, false};
        if (type_name == "f32") return {TypeKind::Float, 32, true, false};
        if (type_name == "f64") return {TypeKind::Float, 64, true, false};
        if (type_name == "str") return {TypeKind::String, 0, false, false};
        if (type_name == "string") return {TypeKind::String, 0, false, false};
        if (type_name == "bool") return {TypeKind::Bool, 0, false, false};
        if (type_name == "auto") return {TypeKind::Auto, 0, false, false};
        
        throw std::runtime_error("Unknown type: " + type_name);
    }
    
    bool types_compatible(const TypeInfo& expected, const TypeInfo& actual) {
        // Simple type compatibility check
        // Could be expanded for more complex cases
        if (expected.kind == TypeKind::Auto) return true;
        if (expected.kind != actual.kind) return false;
        if (expected.kind == TypeKind::Int || expected.kind == TypeKind::Float) {
            return expected.width == actual.width && 
                   expected.is_signed == actual.is_signed;
        }
        return true;
    }
};
