#include "../../src/ctelnet.h"
#include "../../src/Host.h"
#include "../../3rdparty/edbee-lib/edbee-lib/edbee/texteditorwidget.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {

    new Host(23, "achaea.com", QString(), QString(), 1);
    return 0;
}
