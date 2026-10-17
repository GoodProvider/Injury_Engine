import sys
import argparse
import re
# Turn the "Show torture overlay" default off in staged release copies (FOMOD core).
parser = argparse.ArgumentParser(description='Sets the overlay default to false in manifest.yaml and whipping.json.')
parser.add_argument('manifest', type=str)
parser.add_argument('whipping', type=str)
args = parser.parse_args()

def rewrite(path, pattern, repl):
    with open(path, 'r', encoding='utf-8', newline='') as fin:
        text = fin.read()
    text, n = re.subn(pattern, repl, text)
    if n != 1:
        print("error:", path, "matched", n, "times, expected 1")
        sys.exit(1)
    with open(path, 'w', encoding='utf-8', newline='') as fout:
        fout.write(text)
    print("overlay default off:", path)

# defaultValue line of the block whose path is injury_engine.overlay.enabled
rewrite(args.manifest,
        r'(path:\s*"injury_engine\.overlay\.enabled"[\s\S]*?defaultValue:\s*)true',
        r'\1false')
rewrite(args.whipping, r'("overlay_enabled"\s*:\s*)true', r'\1false')
