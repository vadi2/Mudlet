
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[áéíóúñ]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "áéíóúñ");
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[áéíóúñ]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "áéíóúñ");
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "printCmdLine([[tell Zugg ]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "tell Zugg ");
  }

  void testSimpleSend() {
    // <SEND>north</SEND>
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
    QCOMPARE(stub.mHrefs[0], "send([[north]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "north");
  }

  void testSendPrompt() {
    // <SEND href="&text;" PROMPT>north</SEND>
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
    QCOMPARE(stub.mHrefs[0], "printCmdLine([[north]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "north");
  }

  void testSendHrefTextEntity() {
    // Example from Age of Elements
    QString input = "<send 'push &text;' HINT='push button'>button</send>";

    TMxpTagProcessor processor;
    TMxpStubClient stub;

    auto nodes = TMxpTagParser::parseToMxpNodeList(input, false);
    for (const auto &node : nodes) {
      processor.handleNode(processor, stub, node.get());
    }

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[push button]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "push button");
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[say I am Gandalf]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "say I am Gandalf");
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

    QCOMPARE(stub.mHrefs.size(), 2);
    QCOMPARE(stub.mHrefs[0], "send([[look]])");
    QCOMPARE(stub.mHrefs[1], "send([[say hello]])");

    QCOMPARE(stub.mHints.size(), 2);
    QCOMPARE(stub.mHints[0], "LOOK AROUND");
    QCOMPARE(stub.mHints[1], "SAY HELLO");

    // Now add top menu entries

    ctx.getEntityResolver().registerEntity("&frontHint;", "WHO IS ONLINE?|");
    ctx.getEntityResolver().registerEntity("&frontHref;", "who|");

    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mHrefs.size(), 3);
    QCOMPARE(stub.mHrefs[0], "send([[who]])");
    QCOMPARE(stub.mHrefs[1], "send([[look]])");
    QCOMPARE(stub.mHrefs[2], "send([[say hello]])");

    QCOMPARE(stub.mHints.size(), 3);
    QCOMPARE(stub.mHints[0], "WHO IS ONLINE?");
    QCOMPARE(stub.mHints[1], "LOOK AROUND");
    QCOMPARE(stub.mHints[2], "SAY HELLO");

    // Finally add something to the end of the menu

    ctx.getEntityResolver().registerEntity("&backHints;",
                                           "|KNOCK AT THE DOOR|BREAK THE DOOR");
    ctx.getEntityResolver().registerEntity("&backHrefs;",
                                           "|knock at door|break door");

    tagHandler.handleTag(ctx, stub, startTag->asStartTag());
    tagHandler.handleContent("TAG CONTENT");
    tagHandler.handleTag(ctx, stub, endTag->asEndTag());

    QCOMPARE(stub.mHrefs.size(), 5);
    QCOMPARE(stub.mHrefs[0], "send([[who]])");
    QCOMPARE(stub.mHrefs[1], "send([[look]])");
    QCOMPARE(stub.mHrefs[2], "send([[say hello]])");
    QCOMPARE(stub.mHrefs[3], "send([[knock at door]])");
    QCOMPARE(stub.mHrefs[4], "send([[break door]])");

    QCOMPARE(stub.mHints.size(), 5);
    QCOMPARE(stub.mHints[0], "WHO IS ONLINE?");
    QCOMPARE(stub.mHints[1], "LOOK AROUND");
    QCOMPARE(stub.mHints[2], "SAY HELLO");
    QCOMPARE(stub.mHints[3], "KNOCK AT THE DOOR");
    QCOMPARE(stub.mHints[4], "BREAK THE DOOR");
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

    QCOMPARE(stub.mHrefs.size(), 2);
    QCOMPARE(stub.mHrefs[0], "send([[PROBE SUSPENDERS30901]])");
    QCOMPARE(stub.mHrefs[1], "send([[BUY SUSPENDERS30901]])");

    QCOMPARE(stub.mHints.size(), 2);
    QCOMPARE(stub.mHints[0], "PROBE SUSPENDERS30901");
    QCOMPARE(stub.mHints[1], "BUY SUSPENDERS30901");
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[east]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "east"); // Should be "east", not "HREF"
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

    QCOMPARE(stub.mHrefs.size(), 1);
    QCOMPARE(stub.mHrefs[0], "send([[west]])");

    QCOMPARE(stub.mHints.size(), 1);
    QCOMPARE(stub.mHints[0], "west"); // Should be "west"
  }

  void testSendHrefWithLuaBracketInjection() {
    // A malicious server could craft a SEND href containing ]] to break out of
    // the Lua long-bracket string and inject arbitrary code.
    // The href value is interpolated raw into send([[...]]), so ]] terminates
    // the string early, allowing code injection.
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

    QCOMPARE(stub.mHrefs.size(), 1);
    // The resulting href must not contain an unescaped ]] that would allow
    // breaking out of the Lua long-bracket string and injecting code.
    // A proper fix would use a quoting mechanism that prevents ]] from
    // terminating the string literal.
    QVERIFY2(!stub.mHrefs[0].contains(qsl("]])os.execute")),
             qPrintable(qsl("Lua code injection detected in SEND href: %1")
                            .arg(stub.mHrefs[0])));
  }

  void testSendHrefWithNestedBrackets() {
    // Test that ]] in various positions within the href is handled safely,
    // not just as part of a specific injection payload.
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

    QCOMPARE(stub.mHrefs.size(), 1);
    // After wrapping in send([[...]]), the raw ]] would prematurely close the
    // long-bracket string. The href must be sanitized so this cannot happen.
    QVERIFY2(!stub.mHrefs[0].contains(qsl("send([[]]")),
             qPrintable(
                 qsl("Unescaped ]] at start of SEND href breaks Lua string: %1")
                     .arg(stub.mHrefs[0])));

    // Case 2: ]] in the middle of href
    auto startTag2 = parseNode(R"(<SEND href="say hello]]world">)");
    auto endTag2 = parseNode("</SEND>");
    QVERIFY(startTag2);
    QVERIFY(endTag2);

    tagHandler.handleTag(ctx, stub, startTag2->asStartTag());
    tagHandler.handleContent("test2");
    tagHandler.handleTag(ctx, stub, endTag2->asEndTag());

    QCOMPARE(stub.mHrefs.size(), 1);
    // Count occurrences of ]] in the href - there should be exactly one,
    // the legitimate closing ]] at the end of the long-bracket string.
    // If ]] appears earlier, it breaks the Lua string.
    int closingBracketCount = stub.mHrefs[0].count(qsl("]]"));
    QVERIFY2(closingBracketCount == 1,
             qPrintable(
                 qsl("SEND href contains %1 occurrences of ]] (expected 1): %2")
                     .arg(closingBracketCount)
                     .arg(stub.mHrefs[0])));

    // Case 3: Multiple ]] sequences
    auto startTag3 = parseNode(R"(<SEND href="a]]))b]]c">)");
    auto endTag3 = parseNode("</SEND>");
    QVERIFY(startTag3);
    QVERIFY(endTag3);

    tagHandler.handleTag(ctx, stub, startTag3->asStartTag());
    tagHandler.handleContent("test3");
    tagHandler.handleTag(ctx, stub, endTag3->asEndTag());

    QCOMPARE(stub.mHrefs.size(), 1);
    closingBracketCount = stub.mHrefs[0].count(qsl("]]"));
    QVERIFY2(closingBracketCount == 1,
             qPrintable(
                 qsl("SEND href contains %1 occurrences of ]] (expected 1): %2")
                     .arg(closingBracketCount)
                     .arg(stub.mHrefs[0])));
  }

  void testLinkHrefWithLuaBracketInjection() {
    // Same vulnerability exists in TMxpLinkTagHandler: server-controlled href
    // is wrapped in openUrl([[...]]) with no sanitization, allowing ]] to
    // break out of the Lua string and inject arbitrary code.
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

    QCOMPARE(stub.mHrefs.size(), 1);
    // The resulting href must not allow code injection via unescaped ]]
    QVERIFY2(!stub.mHrefs[0].contains(qsl("]])os.execute")),
             qPrintable(qsl("Lua code injection detected in A href: %1")
                            .arg(stub.mHrefs[0])));

    // Additionally verify that the href has exactly one ]] (the legitimate
    // closing bracket of the long-bracket string)
    int closingBracketCount = stub.mHrefs[0].count(qsl("]]"));
    QVERIFY2(
        closingBracketCount == 1,
        qPrintable(qsl("A href contains %1 occurrences of ]] (expected 1): %2")
                       .arg(closingBracketCount)
                       .arg(stub.mHrefs[0])));
  }
};

#include "TMxpSendTagHandlerTest.moc"
QTEST_MAIN(TMxpSendTagHandlerTest)
