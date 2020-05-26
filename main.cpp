#include <iostream>
#include <sstream>

#include <llvm/Support/CommandLine.h>

class CastCallBack : public MatchFinder::MatchCallback {
public:
    CastCallBack(Rewriter& rewriter) : _rewriter(rewriter) {};

    virtual void run(const MatchFinder::MatchResult &Result) {

    const auto *CExp = Result.Nodes.getNodeAs<CStyleCastExpr>("cast");

    SourceManager &SourMan = *Result.SourceManager;

    auto ReplaceRange = CharSourceRange::getCharRange (
		    CExp->getLParenLoc(), 
		    CExp->getSubExprAsWritten()->getBeginLoc());

    StringRef DestTypeString = Lexer::getSourceText(CharSourceRange::getTokenRange(
    	CExp->getLParenLoc().getLocWithOffset(1), 
    	CExp->getRParenLoc().getLocWithOffset(-1)),
    	SourMan, Result.Context->getLangOpts());

    const Expr *Expression = CExp->getSubExprAsWritten()->IgnoreImpCasts();

    std::string CastReplaceText = ("static_cast<" + DestTypeString + ">").str();

    if (!isa<ParenExpr>(Expression)){
    	CastReplaceText.push_back('(');

	_rewriter.InsertText(Lexer::getLocForEndOfToken(Expression->getEndLoc(),
	0,
	*Result.SourceManager,
       	Result.Context->getLangOpts()),
	")");
    }

    _rewriter.ReplaceText(ReplaceRange, CastReplaceText);
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