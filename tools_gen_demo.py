#!/usr/bin/env python3
"""Generate helpers/demo_card_data.h — the byte-for-byte card responses used by
Moneta's demo mode and by the host tests.

Demo mode does not fake a result. It feeds these blobs to exactly the same
parser the radio path feeds, so a demo screenshot is a real parse of a real
EMV response and cannot drift away from what the app actually does.

Run after editing a demo card:

    python3 tools_gen_demo.py
"""

from pathlib import Path

HERE = Path(__file__).resolve().parent


# --------------------------------------------------------------- TLV builder

def tlv(tag: str, value: bytes) -> bytes:
    """One BER-TLV. Emits the long-form length when the value needs it."""
    t = bytes.fromhex(tag)
    n = len(value)
    if n < 0x80:
        length = bytes([n])
    elif n < 0x100:
        length = bytes([0x81, n])
    else:
        length = bytes([0x82, n >> 8, n & 0xFF])
    return t + length + value


def cat(*parts: bytes) -> bytes:
    return b"".join(parts)


def ascii_(s: str) -> bytes:
    return s.encode("ascii")


def bcd(s: str) -> bytes:
    """Digit string to packed BCD, right-padded with the 0xF nibble."""
    if len(s) % 2:
        s += "F"
    return bytes.fromhex(s)


def track2(pan: str, yymm: str, service: str, discretionary: str) -> bytes:
    """Track 2 Equivalent Data: PAN, 'D' separator, YYMM, service code,
    discretionary data, padded to a byte boundary with the 0xF nibble."""
    s = pan + "D" + yymm + service + discretionary
    if len(s) % 2:
        s += "F"
    return bytes.fromhex(s)


def dol(entries) -> bytes:
    """Data Object List: (tag, length) pairs, no values."""
    out = b""
    for tag, length in entries:
        out += bytes.fromhex(tag) + bytes([length])
    return out


def log_record(fmt_entries, values) -> bytes:
    """A transaction-log record: the DOL's fields concatenated, in order, with
    no tags and no delimiters. Each value is padded or truncated to the length
    the format declares, which is what the card itself does."""
    out = b""
    for (_, length), value in zip(fmt_entries, values):
        v = value[:length]
        out += v + b"\x00" * (length - len(v))
    return out


# ------------------------------------------------------------- PPSE / FCI

def ppse(apps) -> bytes:
    """SELECT 2PAY.SYS.DDF01 response. apps is [(aid_hex, label, priority)]."""
    templates = b""
    for aid, label, prio in apps:
        templates += tlv(
            "61",
            cat(
                tlv("4F", bytes.fromhex(aid)),
                tlv("50", ascii_(label)),
                tlv("87", bytes([prio])),
            ),
        )
    return tlv(
        "6F",
        cat(
            tlv("84", ascii_("2PAY.SYS.DDF01")),
            tlv("A5", tlv("BF0C", templates)),
        ),
    )


def fci(aid_hex: str, label: str, pdol_entries=None, log_sfi=None, log_count=0) -> bytes:
    """SELECT <AID> response."""
    prop = cat(tlv("50", ascii_(label)), tlv("87", b"\x01"), tlv("5F2D", ascii_("en")))
    if pdol_entries:
        prop += tlv("9F38", dol(pdol_entries))
    if log_sfi is not None:
        prop += tlv("BF0C", tlv("9F4D", bytes([log_sfi, log_count])))
    return tlv("6F", cat(tlv("84", bytes.fromhex(aid_hex)), tlv("A5", prop)))


def gpo(aip: bytes, afl: bytes) -> bytes:
    """GET PROCESSING OPTIONS, format 2 (the 77 template)."""
    return tlv("77", cat(tlv("82", aip), tlv("94", afl)))


# ------------------------------------------------------------- the cards

LOG_FMT = [("9A", 3), ("9F02", 6), ("5F2A", 2), ("9F4E", 20)]


def build_cards():
    cards = []

    # 1. The ordinary case. A high-street Visa debit card: full number, full
    #    expiry, and the issuer politely declining to send a name. This is what
    #    most people's wallets will produce, and it still grades a D.
    cards.append(
        dict(
            key="typical",
            title="High-street debit",
            blurb="What most wallets do",
            ppse=ppse([("A0000000031010", "VISA DEBIT", 1)]),
            fci=fci(
                "A0000000031010",
                "VISA DEBIT",
                pdol_entries=[("9F66", 4), ("9F02", 6), ("9F1A", 2), ("95", 5), ("5F2A", 2),
                              ("9A", 3), ("9C", 1), ("9F37", 4)],
            ),
            gpo=gpo(bytes.fromhex("3900"), bytes.fromhex("08010100")),
            records=[
                tlv(
                    "70",
                    cat(
                        tlv("57", track2("4761739001010119", "2809", "201", "0000993")),
                        tlv("5F20", ascii_("UNKNOWN")),
                        tlv("5F24", bcd("280930")),
                        tlv("5F28", bcd("0826")),
                        tlv("9F42", bcd("0826")),
                        tlv("8C", dol([("9F02", 6), ("9F03", 6), ("9F1A", 2)])),
                    ),
                )
            ],
            atc=bytes.fromhex("0271"),
            pin_try=bytes.fromhex("03"),
            log_fmt=None,
            logs=[],
        )
    )

    # 2. The bad case. A credit card that sends a real cardholder name and keeps
    #    a readable transaction log complete with merchant names — a spending
    #    diary anyone can read off your hip.
    cards.append(
        dict(
            key="chatty",
            title="Talkative credit",
            blurb="Name and spending log",
            ppse=ppse([("A0000000041010", "MASTERCARD", 1), ("A0000000043060", "MAESTRO", 2)]),
            fci=fci(
                "A0000000041010",
                "MASTERCARD",
                pdol_entries=[("9F66", 4), ("9F02", 6), ("9F37", 4)],
                log_sfi=0x0B,
                log_count=10,
            ),
            gpo=gpo(bytes.fromhex("1980"), bytes.fromhex("10010301")),
            records=[
                tlv(
                    "70",
                    cat(
                        tlv("57", track2("5413339000001513", "2711", "221", "1000001")),
                        tlv("5F20", ascii_("R PATEL")),
                        tlv("5F24", bcd("271130")),
                        tlv("5F28", bcd("0356")),
                        tlv("9F42", bcd("0356")),
                    ),
                )
            ],
            atc=bytes.fromhex("00E3"),
            pin_try=bytes.fromhex("03"),
            log_fmt=dol(LOG_FMT),
            logs=[
                log_record(LOG_FMT, [bcd("260714"), bcd("000000004250"), bcd("0356"),
                                     ascii_("CAFE COFFEE DAY")]),
                log_record(LOG_FMT, [bcd("260713"), bcd("000000128900"), bcd("0356"),
                                     ascii_("BIG BAZAAR PUNE")]),
                log_record(LOG_FMT, [bcd("260711"), bcd("000000006000"), bcd("0356"),
                                     ascii_("METRO STN GATE 2")]),
                log_record(LOG_FMT, [bcd("260709"), bcd("000000089950"), bcd("0356"),
                                     ascii_("APOLLO PHARMACY")]),
            ],
        )
    )

    # 3. The card that behaves. The number is not in any readable record — the
    #    issuer keeps it for the online-authorisation path only — so a reader
    #    walks away with the scheme and nothing that identifies an account.
    #    These exist. That is the whole argument for asking your bank.
    cards.append(
        dict(
            key="quiet",
            title="Well-behaved card",
            blurb="An issuer that says no",
            ppse=ppse([("A0000000651010", "JCB CARD", 1)]),
            fci=fci("A0000000651010", "JCB CARD", pdol_entries=[("9F66", 4), ("9F37", 4)]),
            gpo=gpo(bytes.fromhex("2000"), bytes.fromhex("08010100")),
            records=[tlv("70", cat(tlv("9F07", bytes.fromhex("FF00")), tlv("8C", dol([("9F37", 4)]))))],
            atc=None,
            pin_try=None,
            log_fmt=None,
            logs=[],
        )
    )

    return cards


# ------------------------------------------------------------------ emit

def c_array(name: str, data: bytes, indent="    ") -> str:
    if not data:
        return ""
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    cards = build_cards()
    out = [
        "/* Generated by tools_gen_demo.py — do not edit by hand.",
        " *",
        " * Byte-for-byte EMV responses for Moneta's demo cards. Demo mode replays",
        " * these through the same parser the radio path uses, so what a demo",
        " * screenshot shows is a genuine parse and not a mock-up.",
        " */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
    ]

    for c in cards:
        k = c["key"]
        out.append(f"/* --- {c['title']}: {c['blurb']} --- */")
        out.append(c_array(f"demo_{k}_ppse", c["ppse"]))
        out.append(c_array(f"demo_{k}_fci", c["fci"]))
        out.append(c_array(f"demo_{k}_gpo", c["gpo"]))
        for i, rec in enumerate(c["records"]):
            out.append(c_array(f"demo_{k}_rec{i}", rec))
        if c["atc"]:
            out.append(c_array(f"demo_{k}_atc", c["atc"]))
        if c["pin_try"]:
            out.append(c_array(f"demo_{k}_pin", c["pin_try"]))
        if c["log_fmt"]:
            out.append(c_array(f"demo_{k}_logfmt", c["log_fmt"]))
            for i, rec in enumerate(c["logs"]):
                out.append(c_array(f"demo_{k}_log{i}", rec))
        out.append("")

    # A table so the C side can iterate the demo cards without repeating itself.
    out.append("typedef struct {")
    out.append("    const char* title;")
    out.append("    const char* blurb;")
    out.append("    const uint8_t* ppse;")
    out.append("    size_t ppse_len;")
    out.append("    const uint8_t* fci;")
    out.append("    size_t fci_len;")
    out.append("    const uint8_t* gpo;")
    out.append("    size_t gpo_len;")
    out.append("    const uint8_t* recs[2];")
    out.append("    size_t rec_lens[2];")
    out.append("    uint8_t rec_num;")
    out.append("    const uint8_t* atc;")
    out.append("    size_t atc_len;")
    out.append("    const uint8_t* pin;")
    out.append("    size_t pin_len;")
    out.append("    const uint8_t* log_fmt;")
    out.append("    size_t log_fmt_len;")
    out.append("    const uint8_t* logs[6];")
    out.append("    size_t log_lens[6];")
    out.append("    uint8_t log_num;")
    out.append("    uint8_t apdu_count;")
    out.append("} DemoCardData;")
    out.append("")
    out.append("static const DemoCardData demo_cards[] = {")

    for c in cards:
        k = c["key"]
        # One SELECT PPSE, one SELECT AID, one GPO, one READ RECORD each, plus
        # the GET DATA calls and the log reads: the real command count.
        apdus = 3 + len(c["records"])
        apdus += 1 if c["atc"] else 0
        apdus += 1 if c["pin_try"] else 0
        apdus += (1 + len(c["logs"])) if c["log_fmt"] else 0

        out.append("    {")
        out.append(f'        .title = "{c["title"]}",')
        out.append(f'        .blurb = "{c["blurb"]}",')
        out.append(f"        .ppse = demo_{k}_ppse, .ppse_len = sizeof(demo_{k}_ppse),")
        out.append(f"        .fci = demo_{k}_fci, .fci_len = sizeof(demo_{k}_fci),")
        out.append(f"        .gpo = demo_{k}_gpo, .gpo_len = sizeof(demo_{k}_gpo),")
        recs = ", ".join(f"demo_{k}_rec{i}" for i in range(len(c["records"])))
        lens = ", ".join(f"sizeof(demo_{k}_rec{i})" for i in range(len(c["records"])))
        out.append(f"        .recs = {{{recs}}}, .rec_lens = {{{lens}}},")
        out.append(f"        .rec_num = {len(c['records'])},")
        if c["atc"]:
            out.append(f"        .atc = demo_{k}_atc, .atc_len = sizeof(demo_{k}_atc),")
        if c["pin_try"]:
            out.append(f"        .pin = demo_{k}_pin, .pin_len = sizeof(demo_{k}_pin),")
        if c["log_fmt"]:
            out.append(
                f"        .log_fmt = demo_{k}_logfmt, .log_fmt_len = sizeof(demo_{k}_logfmt),"
            )
            logs = ", ".join(f"demo_{k}_log{i}" for i in range(len(c["logs"])))
            llens = ", ".join(f"sizeof(demo_{k}_log{i})" for i in range(len(c["logs"])))
            out.append(f"        .logs = {{{logs}}}, .log_lens = {{{llens}}},")
            out.append(f"        .log_num = {len(c['logs'])},")
        out.append(f"        .apdu_count = {apdus},")
        out.append("    },")

    out.append("};")
    out.append("")
    out.append("#define MONETA_DEMO_CARD_NUM (sizeof(demo_cards) / sizeof(demo_cards[0]))")
    out.append("")

    path = HERE / "helpers" / "demo_card_data.h"
    path.write_text("\n".join(out))
    print(f"wrote {path} ({path.stat().st_size} bytes, {len(cards)} cards)")


if __name__ == "__main__":
    main()
