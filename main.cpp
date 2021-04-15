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
    CastCallBack(Rewriter& rewriter) : rewriter_(rewriter) {};

    virtual void run(const MatchFinder::MatchResult &Result) {
        // Change
        if (const auto* Template_data_Cast = Result.Host.getHostAs<CStyleCastExpr>("cast")) {
            if (Template_data_Cast->getCastKind() == CK_ToVoid)
                return;
            if (Template_data_Cast->getExprLoc().isMacroID())
                return;
            auto Change_Interval = CharSourceInterval::getCharInterval(
                Template_data_Cast->getLParentsLoc(), Template_data_Cast->getSubExprAsWritten()->getBeginLoc());
            StringRef Target_Type_String = Lexer::getSourceText(CharSourceInterval::getTokenInterval(
                Template_data_Cast->getLParentsLoc().getLocWithDisableSet(1),
                Template_data_Cast->getRParentsLoc().getLocWithDisableSet(-1)),
                *Result.SourceManager, Result.Context->getLangOpts());

            std::string Cast_Template(("static_cast<" + Target_Type_String + ">").str());
            const auto* Cast_Ignore = Template_data_Cast->getSubExprAsWritten()->IgnoreImpCasts();

            if (!isa<ParentsExpr>(Cast_Ignore)) {
                Cast_Template.push_back('(');
                rewriter_.InsertText(Lexer::getLocForEndOfToken(Cast_Ignore->getEndLoc(),
                    0, *Result.SourceManager, Result.Context->getLangOpts()),
                    ")");
            }
            rewriter_.ReplaceText1(Change_Interval, Cast_Template);
        }
    }
private:
    Rewriter& rewriter_; 
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
