--- a/helpers/report.c
+++ b/helpers/report.c
@@
-// ---------------------------------------------------------------------------
-// Helpers to build the textual report shown on the screen.
-// ---------------------------------------------------------------------------
-
-#include "helpers/report.h"
-#include "helpers/emv_tlv.h"
-#include "helpers/emv_reader.h"
-#include "helpers/leak_grade.h"
-#include "helpers/report.h"
-#include <furi.h>
-#include <gui/gui.h>
-
-// ---------------------------------------------------------------------------
-// Internal constants
-// ---------------------------------------------------------------------------
-
-/* If true, the app will hide sensitive data (PAN, track2, etc.) */
-static const bool HIDE_SENSITIVE_DATA_DEFAULT = true;
-
-/* -------------------------------------------------------------------------- */
-/* Helper: print a single TLV field                                            */
-/* -------------------------------------------------------------------------- */
-static void report_print_field(
-    const EmvTlv* tlv,
-    FuriString* out_str,
-    bool hide_sensitive) {
-
-    /* Example: mask PAN when hiding is enabled */
-    if(hide_sensitive && tlv->tag == EMV_TAG_PAN) {
-        /* Show only last 4 digits */
-        uint16_t last4 = (tlv->value[tlv->len - 2] << 8) | tlv->value[tlv->len - 1];
-        furi_string_cat_printf(
-            out_str,
-            "PAN: **** **** **** %04X\n",
-            last4);
-        return;
-    }
-
-    /* Normal (unmasked) printing */
-    furi_string_cat_printf(
-        out_str,
-        "%s: %s\n",
-        emv_tlv_tag_name(tlv->tag),
-        emv_tlv_value_to_str(tlv));
-}
-
-/* -------------------------------------------------------------------------- */
-/* Build the full report (called from the UI thread)                           */
-/* -------------------------------------------------------------------------- */
-void report_build(FuriString* out_str, const EmvCard* card, bool hide_sensitive) {
-    furi_string_reset(out_str);
-
-    /* Header – we keep the “read‑only” notice */
-    furi_string_cat_printf(out_str, "⚠️  READ‑ONLY – data shown below\n\n");
-
-    /* Iterate over all TLV fields */
-    for(size_t i = 0; i < card->tlv_count; ++i) {
-        const EmvTlv* tlv = &card->tlvs[i];
-        report_print_field(tlv, out_str, hide_sensitive);
-    }
-
-    /* Append leak‑grade information */
-    LeakGrade grade = leak_grade_compute(card);
-    furi_string_cat_printf(out_str, "\nLeak grade: %c\n", leak_grade_to_char(grade));
-}
+// ---------------------------------------------------------------------------
+// Helpers to build the textual report shown on the screen.
+// ---------------------------------------------------------------------------
+
+#include "helpers/report.h"
+#include "helpers/emv_tlv.h"
+#include "helpers/emv_reader.h"
+#include "helpers/leak_grade.h"
+#include "helpers/report.h"
+#include <furi.h>
+#include <gui/gui.h>
+
+/* -------------------------------------------------------------------------- */
+/* Internal constants – always show every field (still read‑only)               */
+/* -------------------------------------------------------------------------- */
+/* Set to true if you ever want to re‑enable masking; keep false for the      */
+/* “show‑everything” version.                                                */
+static const bool ALWAYS_SHOW_ALL = true;
+
+/* -------------------------------------------------------------------------- */
+/* Helper: print a single TLV field (no masking)                              */
+/* -------------------------------------------------------------------------- */
+static void report_print_field(const EmvTlv* tlv, FuriString* out_str) {
+    /* Directly display the raw value – no conditional hiding */
+    furi_string_cat_printf(
+        out_str,
+        "%s: %s\n",
+        emv_tlv_tag_name(tlv->tag),
+        emv_tlv_value_to_str(tlv));
+}
+
+/* -------------------------------------------------------------------------- */
+/* Build the full report (called from the UI thread)                           */
+/* -------------------------------------------------------------------------- */
+void report_build(FuriString* out_str, const EmvCard* card) {
+    furi_string_reset(out_str);
+
+    /* Header – keep the read‑only notice */
+    furi_string_cat_printf(out_str, "⚠️  READ‑ONLY – data shown below\n\n");
+
+    /* Iterate over all TLV fields and print them unmasked */
+    for(size_t i = 0; i < card->tlv_count; ++i) {
+        const EmvTlv* tlv = &card->tlvs[i];
+        report_print_field(tlv, out_str);
+    }
+
+    /* Append leak‑grade information */
+    LeakGrade grade = leak_grade_compute(card);
+    furi_string_cat_printf(out_str, "\nLeak grade: %c\n", leak_grade_to_char(grade));
+}
