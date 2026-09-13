--- a/helpers/report.h
+++ b/helpers/report.h
@@
-#pragma once
-
-#include <furi.h>
-#include "helpers/emv_card.h"
-
-/** Build a textual report from an EmvCard.
- *  The string is cleared before writing.
- *
- *  @param out_str   Destination FuriString (will be cleared first)
- *  @param card      Source EmvCard data
- *  @param hide_sensitive  If true, mask sensitive fields (PAN, etc.)
- */
-void report_build(FuriString* out_str, const EmvCard* card, bool hide_sensitive);
+#pragma once
+
+#include <furi.h>
+#include "helpers/emv_card.h"
+
+/** Build a textual report from an EmvCard.
+ *  The function now always displays every TLV field (still read‑only).
+ *
+ *  @param out_str   Destination FuriString (will be cleared first)
+ *  @param card      Source EmvCard data
+ */
+void report_build(FuriString* out_str, const EmvCard* card);
