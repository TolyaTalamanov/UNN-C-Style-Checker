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
public:
    CastCallBack(Rewriter& rewriter) : rewriter_cast(rewriter) {
    };

    virtual void run(const MatchFinder::MatchResult &Result) {
        const auto *ExpCast = Result.Nodes.getNodeAs<CStyleCastExpr>("cast");
        if (ExpCast != nullptr) {
            // looking for a place with a cast that we need to replace
            auto range = CharSourceRange::getCharRange (ExpCast->getLParenLoc(),ExpCast->getSubExprAsWritten()->getBeginLoc());

            // looking for info about what type we want to lead(~cast) to
            StringRef text = Lexer::getSourceText(CharSourceRange::getTokenRange(ExpCast->getLParenLoc().getLocWithOffset(1), 
                ExpCast->getRParenLoc().getLocWithOffset(-1)), *Result.SourceManager, Result.Context->getLangOpts());

            std::string string_final = ("static_cast<" + text + ">").str();
            const Expr *SubExpression = ExpCast->getSubExprAsWritten()->IgnoreImpCasts();

            if(!isa<ParenExpr>(SubExpression))
            {
                string_final.push_back('(');
                rewriter_cast.InsertText(Lexer::getLocForEndOfToken(SubExpression->getEndLoc(),
                                        0,
                                        *Result.SourceManager,
                                        Result.Context->getLangOpts()), ")");
                // string_final.push_back(')');
            }
            rewriter_cast.ReplaceText(range, string_final);
        }
    }
private:
    Rewriter& rewriter_cast;
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
