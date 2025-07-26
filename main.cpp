int main() {
 
    auto func = std::make_shared<FunctionNode>();
    func->name = "main";
    func->return_type = "void";
    func->line = 1;
    

    auto let_a = std::make_shared<LetDeclarationNode>();
    let_a->name = "a";
    let_a->type_annotation = "i32";
    let_a->initializer = std::make_shared<IntegerLiteralNode>(42);
    let_a->line = 2;
    func->body.push_back(let_a);
    

    SemanticAnalyzer analyzer;
    try {
        analyzer.analyze(func);
        std::cout << "Semantic analysis passed successfully!" << std::endl;
    } catch (const SemanticError& e) {
        std::cerr << "Semantic error at line " << e.line << ": " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
