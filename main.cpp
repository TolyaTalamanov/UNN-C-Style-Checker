#include <iostream>
#include <sstream>

#include <llvm/Support/CommandLine.h>

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

class CastCallBack : public MatchFinder::MatchCallback {
public:
  CastCallBack(Rewriter &rewriter) : rewriter_(rewriter) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    if (const auto *castExpr = Result.Nodes.getNodeAs<CStyleCastExpr>("cast")) {
      if (castExpr->getExprLoc().isMacroID())
        return;

      if (castExpr->getCastKind() == CK_ToVoid)
        return;

      auto &sManager = *Result.SourceManager;

      auto charRange = CharSourceRange::getCharRange(
          castExpr->getLParenLoc(),
          castExpr->getSubExprAsWritten()->getBeginLoc());

      auto replaceCast = [&](std::string castName) {
        const Expr *subExpr = castExpr->getSubExprAsWritten()->IgnoreImpCasts();
        if (!isa<ParenExpr>(subExpr)) {
          castName.push_back('(');
          rewriter_.InsertText(
              Lexer::getLocForEndOfToken(subExpr->getEndLoc(), 0, sManager,
                                         Result.Context->getLangOpts()),
              ")");
        }
        rewriter_.ReplaceText(charRange, castName);
      };

      auto destTypeString = Lexer::getSourceText(
          CharSourceRange::getTokenRange(
              castExpr->getLParenLoc().getLocWithOffset(1),
              castExpr->getRParenLoc().getLocWithOffset(-1)),
          sManager, Result.Context->getLangOpts());

      const auto DestTypeAsWritten =
          castExpr->getTypeAsWritten().getUnqualifiedType();
      const auto SourceTypeAsWritten =
          castExpr->getSubExprAsWritten()->getType().getUnqualifiedType();
      const auto SourceType = SourceTypeAsWritten.getCanonicalType();
      const auto DestType = DestTypeAsWritten.getCanonicalType();

      if (constCastCheck(SourceType, DestType)) {
        replaceCast("const_cast<" + destTypeString.str() + ">");
      } else {
        replaceCast("static_cast<" + destTypeString.str() + ">");
      }
    }
  }

private:
  static bool constCastCheck(QualType sourceType, QualType destType) {
    while ((sourceType->isPointerType() && destType->isPointerType()) ||
           (sourceType->isReferenceType() && destType->isReferenceType())) {
      sourceType = sourceType->getPointeeType();
      destType = destType->getPointeeType();
      if (sourceType.isConstQualified() && !destType.isConstQualified()) {
        return (sourceType->isPointerType() == destType->isPointerType()) &&
               (sourceType->isReferenceType() == destType->isReferenceType());
      }
    }
    return false;
  }

private:
  Rewriter &rewriter_;
};

class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(Rewriter &rewriter) : callback_(rewriter) {
    matcher_.addMatcher(
        cStyleCastExpr(unless(isExpansionInSystemHeader())).bind("cast"),
        &callback_);
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

  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, StringRef /* file */) override {
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

  return Tool.run(
      newFrontendActionFactory<CStyleCheckerFrontendAction>().get());
}
