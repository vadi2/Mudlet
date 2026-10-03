#!/usr/bin/env python3
"""Builds SwedishTranslator.mpackage from the Lua sources in scripts/.

Usage: python3 build.py [output directory]   (default: this directory)
"""

import os
import re
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
NAME = "SwedishTranslator"

# "sv <text>" and "sv:<command>". A bare "sv" is deliberately not matched: it is
# the usual southwest exit in Swedish-language games, so it must reach the game.
ALIAS_PATTERN = r"^sv(?::\S*(?:\s.*)?|\s+\S.*)$"
ALIAS_SCRIPT = "SwedishTranslator.onCommand(matches[1])"
# Any line with something visible on it; the script decides whether it is Swedish.
# It ships disabled: the scripts switch it on only while auto-translate is on.
# The name must match ST.autoTrigger in scripts/05-learning.lua.
TRIGGER_NAME = "SwedishTranslator auto-translate"
TRIGGER_PATTERN = r"\S"
TRIGGER_SCRIPT = "SwedishTranslator.onGameLine(line)"

TRIGGER_ATTRIBUTES = (
    'isTempTrigger="no" isMultiline="no" isPerlSlashGOption="no" isColorizerTrigger="no" '
    'isFilterTrigger="no" isSoundTrigger="no" isColorTrigger="no" isColorTriggerFg="no" isColorTriggerBg="no"'
)


def trigger_xml(name, is_folder, script="", pattern=None, children="", active=True):
    tag = "TriggerGroup" if is_folder else "Trigger"
    patterns = (
        f"<regexCodeList><string>{escape(pattern)}</string></regexCodeList>"
        "<regexCodePropertyList><integer>1</integer></regexCodePropertyList>"
        if pattern
        else "<regexCodeList /><regexCodePropertyList />"
    )
    return (
        f'<{tag} isActive="{"yes" if active else "no"}" isFolder="{"yes" if is_folder else "no"}" {TRIGGER_ATTRIBUTES}>'
        f"<name>{escape(name)}</name><script>{escape(script)}</script>"
        "<triggerType>0</triggerType><conditonLineDelta>0</conditonLineDelta><mStayOpen>0</mStayOpen>"
        "<mCommand></mCommand><packageName></packageName><mFgColor>#ff0000</mFgColor><mBgColor>#ffff00</mBgColor>"
        "<mSoundFile></mSoundFile><colorTriggerFgColor>#000000</colorTriggerFgColor>"
        f"<colorTriggerBgColor>#000000</colorTriggerBgColor>{patterns}{children}</{tag}>"
    )


def alias_xml(name, is_folder, script="", pattern="", children=""):
    tag = "AliasGroup" if is_folder else "Alias"
    return (
        f'<{tag} isActive="yes" isFolder="{"yes" if is_folder else "no"}">'
        f"<name>{escape(name)}</name><script>{escape(script)}</script><command></command>"
        f"<packageName></packageName><regex>{escape(pattern)}</regex>{children}</{tag}>"
    )


def script_xml(name, is_folder, script="", children=""):
    tag = "ScriptGroup" if is_folder else "Script"
    return (
        f'<{tag} isActive="yes" isFolder="{"yes" if is_folder else "no"}">'
        f"<name>{escape(name)}</name><packageName></packageName><script>{escape(script)}</script>"
        f"<eventHandlerList />{children}</{tag}>"
    )


def build_xml():
    scripts = ""
    for path in sorted((HERE / "scripts").glob("*.lua")):
        # "03-backend.lua" -> "backend": the numeric prefix only fixes the load order.
        match = re.fullmatch(r"\d+-(.+)", path.stem)
        if not match:
            sys.exit(f"{path.name}: script names must look like NN-name.lua")
        scripts += script_xml(match.group(1), False, path.read_text(encoding="utf-8"))

    trigger = trigger_xml(TRIGGER_NAME, False, TRIGGER_SCRIPT, TRIGGER_PATTERN, active=False)
    triggers = trigger_xml(NAME, True, children=trigger)
    aliases = alias_xml(NAME, True, children=alias_xml("sv command", False, ALIAS_SCRIPT, ALIAS_PATTERN))

    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE MudletPackage>\n<MudletPackage version="1.001">\n'
        f"<TriggerPackage>{triggers}</TriggerPackage>\n"
        "<TimerPackage />\n"
        f"<AliasPackage>{aliases}</AliasPackage>\n"
        "<ActionPackage />\n"
        f"<ScriptPackage>{script_xml(NAME, True, children=scripts)}</ScriptPackage>\n"
        "<KeyPackage />\n"
        "<VariablePackage><HiddenVariables /></VariablePackage>\n"
        "</MudletPackage>\n"
    )


def main():
    out_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / f"{NAME}.mpackage"
    config = (HERE / "config.lua").read_text(encoding="utf-8")
    xml = build_xml()
    # Write beside the target and rename, so a failed build never leaves a
    # half-written package behind.
    handle, temporary = tempfile.mkstemp(dir=out_dir, suffix=".tmp")
    os.close(handle)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("config.lua", config)
            archive.writestr(f"{NAME}.xml", xml)
        # mkstemp creates the file owner-only; give it the usual permissions.
        umask = os.umask(0)
        os.umask(umask)
        os.chmod(temporary, 0o666 & ~umask)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.remove(temporary)
    print(target)


if __name__ == "__main__":
    main()
