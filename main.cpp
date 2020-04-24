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
    CastCallBack(Rewriter& rewriter): _rewriter(rewriter) {
        // Your code goes here
    };

    virtual void run(const MatchFinder::MatchResult &Result) {
        // Your code goes here
	const auto *Item = Result.Nodes.getNodeAs<CStyleCastExpr>("cast");
	SourceManager &SourceM = *Result.SourceManager;	

	//Ищем место с преобразованием, которое нам надо заменить
	auto ReRange = CharSourceRange::getCharRange (Item->getLParenLoc(),
						//Получаем cast expr как оно было написано в исходном коде, просматривая
						//любые неявные приведения или другие промежуточные узлы, введенные
						//семантическим анализом
						      Item->getSubExprAsWritten()->getBeginLoc());

	//Пытаемся выцепить к какому типу хотим привести
	StringRef DestTypeString = 
		//getSourceText - Возвращает строку для источника, охватывающую диапазон
		Lexer::getSourceText(CharSourceRange::getTokenRange(Item->getLParenLoc().getLocWithOffset(1),
								    Item->getRParenLoc().getLocWithOffset(-1)),
		SourceM, Result.Context->getLangOpts());

	std::string str = ("static_cast<" + DestTypeString + ">").str();

	//IgnoreImpCasts - Пропускаем все неявные приведения, которые могу окружать это выражение (берем саму переменную)
	const Expr *SubExpr = Item->getSubExprAsWritten()->IgnoreImpCasts();

	if(!isa<ParenExpr>(SubExpr)) 
	{
		//Нужно написть ту переменную, которую будем приводить к другому типу
		str.push_back('(');
		_rewriter.InsertText(Lexer::getLocForEndOfToken(SubExpr->getEndLoc(),
								0,
								SourceM,
								Result.Context->getLangOpts()),
				     ")");
	}
	_rewriter.ReplaceText(ReRange, str);
    }
private:
	Rewriter& _rewriter;
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
