#include <iostream>
#include <sstream>

#include <llvm/Support/CommandLine.h>

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Frontend/CompilerInstance.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

class CastCallBack : public MatchFinder::MatchCallback {
	Rewriter *rewriter_;
public:
    CastCallBack(Rewriter& rewriter) {
        rewriter_=&rewriter;
    };

    virtual void run(const MatchFinder::MatchResult &Result) {
        SourceManager &SM = *Result.SourceManager;
       	const CStyleCastExpr *cast_expression = Result.Nodes.getNodeAs<CStyleCastExpr>("cast");
	SourceLocation start = cast_expression->getBeginLoc();
	SourceLocation end = cast_expression->getEndLoc();
	
	rewriter_->RemoveText(start,1);
	rewriter_->RemoveText(end.getLocWithOffset(-1),1);
	rewriter_->InsertText(start,"static_cast<");
	rewriter_->InsertText(end,">(");

	const Expr *expr = cast_expression->getSubExprAsWritten();
	rewriter_->InsertText(Lexer::getLocForEndOfToken(expr->getEndLoc(),0,SM,LangOptions()), ")");
    }
};

class MyASTConsumer : public ASTConsumer {
public:
    MyASTConsumer(Rewriter &rewriter) : callback_(rewriter) {
        matcher_.addMatcher(
                cStyleCastExpr(unless(isExpansionInSystemHeader())).bind("cast"), &callback_);
    }

    void HandleTranslationUnit(ASTContext &Context) override {
        matcher_.matchAST(Context);
    }

private:
    CastCallBack callback_;
    MatchFinder matcher_;
};

class CStyleCheckerFrontendAction : public ASTFrontendAction {
public:
    CStyleCheckerFrontendAction() = default;
    void EndSourceFileAction() override {
        rewriter_.getEditBuffer(rewriter_.getSourceMgr().getMainFileID())
            .write(llvm::outs());
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef /* file */) override {
        rewriter_.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<MyASTConsumer>(rewriter_);
    }

private:
    Rewriter rewriter_;
};

static llvm::cl::OptionCategory CastMatcherCategory("cast-matcher options");

int main(int argc, const char **argv) {
    CommonOptionsParser OptionsParser(argc, argv, CastMatcherCategory);
    ClangTool Tool(OptionsParser.getCompilations(),
            OptionsParser.getSourcePathList());

    return Tool.run(newFrontendActionFactory<CStyleCheckerFrontendAction>().get());
}

