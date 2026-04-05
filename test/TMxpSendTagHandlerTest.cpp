
#include "TMxpSendTagHandler.h"
#include "TMxpLinkTagHandler.h"
#include "TMxpProcessor.h"
#include "TMxpStubClient.h"
#include "TMxpTagParser.h"
#include "TMxpTagProcessor.h"
#include <QTest>

class TMxpSendTagHandlerTest : public QObject {
  Q_OBJECT

private:
  QSharedPointer<MxpNode> parseNode(const QString &tagText) const {
    auto nodes = TMxpTagParser::parseToMxpNodeList(tagText);
    return !nodes.isEmpty() ? nodes.first() : nullptr;
  }

private slots:
  void testSendHrefUTF8FromMxpProcessor() {
    // issue #4368
    TMxpStubClient stub;
    TMxpProcessor processor(&stub);
    processor.setMode(MXP_MODE_CODE_LOCK_SECURE);

    std::string input = "<SEND href=\"áéíóúñ\" >test link: áéíóúñ</SEND>";
    for (char &ch : input) {
      processor.processMxpInput(ch, true);
    }

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("áéíóúñ"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("áéíóúñ"));
  }

  void testSendHrefUTF8() {
    // issue #4368
    QString input = "<SEND href=\"áéíóúñ\" >test link: áéíóúñ</SEND>";

    TMxpTagProcessor processor;
    TMxpStubClient stub;

    auto nodes = TMxpTagParser::parseToMxpNodeList(input, false);
    QCOMPARE(nodes.size(), 3);
    for (const auto &node : nodes) {
      processor.handleNode(processor, stub, node.get());
    }

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("áéíóúñ"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("áéíóúñ"));
  }

  void testStaticText() {
    // <SEND "tell Zugg " PROMPT>Zugg</SEND>
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode("<SEND \"tell Zugg \" PROMPT>");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("Zugg");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("printCmdLine"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("tell Zugg "));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("tell Zugg "));
  }

  void testSimpleSend() {
    // <SEND>north</SEND>
    // This uses &text; placeholder, so falls back to code strings for
    // deferred content replacement
    TMxpStubContext ctx;
    TMxpStubClient stub;

    MxpStartTag startTag("SEND");
    MxpEndTag endTag("SEND");

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;

    tagHandler.handleTag(ctx, stub, &startTag);
    tagHandler.handleContent("north");
    tagHandler.handleTag(ctx, stub, &endTag);

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], qsl("send([[north]])"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("north"));
  }

  void testSendPrompt() {
    // <SEND href="&text;" PROMPT>north</SEND>
    // This uses &text; placeholder, so falls back to code strings for
    // deferred content replacement
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode("<SEND href=\"&text;\" PROMPT>");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("north");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], qsl("printCmdLine([[north]])"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("north"));
  }

  void testSendHrefTextEntity() {
    // Example from Age of Elements
    // This uses &text; placeholder, so falls back to code strings for
    // deferred content replacement
    QString input = "<send 'push &text;' HINT='push button'>button</send>";

    TMxpTagProcessor processor;
    TMxpStubClient stub;

    auto nodes = TMxpTagParser::parseToMxpNodeList(input, false);
    for (const auto &node : nodes) {
      processor.handleNode(processor, stub, node.get());
    }

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], qsl("send([[push button]])"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("push button"));
  }

  void testResolvingEntity() {
    TMxpStubContext ctx;
    TMxpStubClient stub;

    ctx.getEntityResolver().registerEntity("&charName;", "Gandalf");

    auto startTag = parseNode("<SEND href=\"say I am &charName;\">");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("say I am Gandalf"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("say I am Gandalf"));
  }

  void testResolvingEntityWithPipe() {
    TMxpStubContext ctx;
    TMxpStubClient stub;

    ctx.getEntityResolver().registerEntity("&frontHint;", "");
    ctx.getEntityResolver().registerEntity("&frontHref;", "");

    ctx.getEntityResolver().registerEntity("&backHints;", "");
    ctx.getEntityResolver().registerEntity("&backHrefs;", "");

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;

    // First check the SEND TAG with empty entities
    auto startTag =
        parseNode("<SEND href=\"&frontHref;look|say hello&backHrefs;\" "
                  "hint=\"&frontHint;LOOK AROUND|SAY HELLO&backHints;\">");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 2);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("look"));
    QCOMPARE(stub.mLinkFunctionArgs[1], qsl("say hello"));

    QCOMPARE(stub.mHints.size(), 2);
    QCOMPARE(stub.mHints[0], qsl("LOOK AROUND"));
    QCOMPARE(stub.mHints[1], qsl("SAY HELLO"));

    // Now add top menu entries

    ctx.getEntityResolver().registerEntity("&frontHint;", "WHO IS ONLINE?|");
    ctx.getEntityResolver().registerEntity("&frontHref;", "who|");

    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 3);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("who"));
    QCOMPARE(stub.mLinkFunctionArgs[1], qsl("look"));
    QCOMPARE(stub.mLinkFunctionArgs[2], qsl("say hello"));

    QCOMPARE(stub.mHints.size(), 3);
    QCOMPARE(stub.mHints[0], qsl("WHO IS ONLINE?"));
    QCOMPARE(stub.mHints[1], qsl("LOOK AROUND"));
    QCOMPARE(stub.mHints[2], qsl("SAY HELLO"));

    // Finally add something to the end of the menu

    ctx.getEntityResolver().registerEntity("&backHints;",
                                           "|KNOCK AT THE DOOR|BREAK THE DOOR");
    ctx.getEntityResolver().registerEntity("&backHrefs;",
                                           "|knock at door|break door");

    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 5);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("who"));
    QCOMPARE(stub.mLinkFunctionArgs[1], qsl("look"));
    QCOMPARE(stub.mLinkFunctionArgs[2], qsl("say hello"));
    QCOMPARE(stub.mLinkFunctionArgs[3], qsl("knock at door"));
    QCOMPARE(stub.mLinkFunctionArgs[4], qsl("break door"));

    QCOMPARE(stub.mHints.size(), 5);
    QCOMPARE(stub.mHints[0], qsl("WHO IS ONLINE?"));
    QCOMPARE(stub.mHints[1], qsl("LOOK AROUND"));
    QCOMPARE(stub.mHints[2], qsl("SAY HELLO"));
    QCOMPARE(stub.mHints[3], qsl("KNOCK AT THE DOOR"));
    QCOMPARE(stub.mHints[4], qsl("BREAK THE DOOR"));
  }

  void testSendHrefHintMismatch() {
    // Example from starmourn on WARES command from NPCs
    // <SEND HREF="PROBE SUSPENDERS30901|BUY SUSPENDERS30901" hint="Click to see
    // command menu">30901</SEND>
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode(
        R"(<SEND HREF="PROBE SUSPENDERS30901|BUY SUSPENDERS30901" hint="Click to see command menu">)");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("3091");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 2);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("PROBE SUSPENDERS30901"));
    QCOMPARE(stub.mLinkFunctionArgs[1], qsl("BUY SUSPENDERS30901"));

    QCOMPARE(stub.mHints.size(), 2);
    QCOMPARE(stub.mHints[0], qsl("PROBE SUSPENDERS30901"));
    QCOMPARE(stub.mHints[1], qsl("BUY SUSPENDERS30901"));
  }

  void testSendExpireHref() {
    // Test case for issue #8383
    // When EXPIRE comes before HREF, the tooltip should show the HREF value,
    // not the literal string "HREF" <SEND EXPIRE="Exits"
    // HREF="east">East</SEND>
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode(R"(<SEND EXPIRE="Exits" HREF="east">)");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("East");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("east"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("east")); // Should be "east", not "HREF"
  }

  void testSendExpireHrefWithoutHint() {
    // Test case for standard order: HREF then EXPIRE
    // <SEND HREF="west" EXPIRE="Exits">West</SEND>
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode(R"(<SEND HREF="west" EXPIRE="Exits">)");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("West");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("west"));

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], qsl("west")); // Should be "west"
  }

  void testSendHrefWithLuaBracketInjection() {
    // A malicious server could craft a SEND href containing ]] to break out of
    // a Lua long-bracket string and inject arbitrary code.
    // With the closure-based fix, the href is stored as data (never compiled as
    // code), so injection is impossible regardless of content.
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag =
        parseNode(R"(<SEND href="]])os.execute('payload')send([[x">)");
    auto endTag = parseNode("</SEND>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("click");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    // The href is now stored as raw data in mLinkFunctionArgs, never as Lua
    // code. It is passed as an upvalue to a closure, so ]] cannot cause code
    // injection.
    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0],
             qsl("]])os.execute('payload')send([[x"));
  }

  void testSendHrefWithNestedBrackets() {
    // Test that ]] in various positions within the href is handled safely,
    // not just as part of a specific injection payload.
    // With the closure-based fix, all hrefs are stored as raw data upvalues,
    // so bracket sequences are irrelevant to security.
    TMxpStubContext ctx;
    TMxpStubClient stub;

    TMxpSendTagHandler sendTagHandler;
    TMxpTagHandler &tagHandler = sendTagHandler;

    // Case 1: ]] at the beginning of href
    auto startTag1 = parseNode(R"(<SEND href="]]..evil..[[">)");
    auto endTag1 = parseNode("</SEND>");
    QVERIFY(startTag1);
    QVERIFY(endTag1);

    tagHandler.handleTag(ctx, stub, startTag1->asStartTag());
    tagHandler.handleContent("test1");
    tagHandler.handleTag(ctx, stub, endTag1->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("]]..evil..[["));

    // Case 2: ]] in the middle of href
    auto startTag2 = parseNode(R"(<SEND href="say hello]]world">)");
    auto endTag2 = parseNode("</SEND>");
    QVERIFY(startTag2);
    QVERIFY(endTag2);

    tagHandler.handleTag(ctx, stub, startTag2->asStartTag());
    tagHandler.handleContent("test2");
    tagHandler.handleTag(ctx, stub, endTag2->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("say hello]]world"));

    // Case 3: Multiple ]] sequences
    auto startTag3 = parseNode(R"(<SEND href="a]]))b]]c">)");
    auto endTag3 = parseNode("</SEND>");
    QVERIFY(startTag3);
    QVERIFY(endTag3);

    tagHandler.handleTag(ctx, stub, startTag3->asStartTag());
    tagHandler.handleContent("test3");
    tagHandler.handleTag(ctx, stub, endTag3->asEndTag());

    QCOMPARE(stub.mLinkFunctionName, qsl("send"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("a]]))b]]c"));
  }

  void testLinkHrefWithLuaBracketInjection() {
    // With the closure-based fix, the href is stored as raw data (never
    // compiled as Lua code), so ]] in the href cannot cause code injection.
    TMxpStubContext ctx;
    TMxpStubClient stub;

    auto startTag = parseNode(R"(<A href="]])os.execute('x')openUrl([[y">)");
    auto endTag = parseNode("</A>");
    QVERIFY(startTag);
    QVERIFY(endTag);

    TMxpLinkTagHandler linkTagHandler;
    TMxpTagHandler &tagHandler = linkTagHandler;
    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("click me");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    // The href is now stored as raw data in mLinkFunctionArgs, never as Lua
    // code.
    QCOMPARE(stub.mLinkFunctionName, qsl("openUrl"));
    QCOMPARE(stub.mLinkFunctionArgs.size(), 1);
    QCOMPARE(stub.mLinkFunctionArgs[0], qsl("]])os.execute('x')openUrl([[y"));
  }
};

#include "TMxpSendTagHandlerTest.moc"
QTEST_MAIN(TMxpSendTagHandlerTest)
