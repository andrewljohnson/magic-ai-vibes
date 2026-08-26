#!/usr/bin/env python3
"""Write pinned-catalog-p29.json from the protocol-29 engine.

    PENTA_ENGINE_DIR=engine-p29 python3 make_pinned_catalog.py

WHY IT IS PINNED. Feature slots come from the SORTED SET OF LEGAL card
definitions, so the layout moves if legality moves. Protocol 29's catalog
marks 981 cards legal against the 128 the nets were trained on -- but all
128 are still present with identical ids, and the 14 built-in decks use
only 107 of them. Restricting `legal` back to those 128 reproduces the
exact layout the trained nets expect (extractor size 1081, action_dim 184).

This only affects OUR feature layout. The engine's own legality is
untouched.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from extractor import import_penta                        # noqa: E402

old = json.load(open(os.path.join(HERE, "pinned-catalog.json")))
keep = {c["definition"] for c in old["cards"] if c.get("legal", True)}
new = json.loads(import_penta().catalog("old-school-93-94"))
for c in new["cards"]:
    c["legal"] = c["definition"] in keep
new["pinnedNote"] = (f"legal restricted to the {len(keep)} definitions the "
                     "trained nets were built on")
out = os.path.join(HERE, "pinned-catalog-p29.json")
json.dump(new, open(out, "w"))
print(f"wrote {out}: {len(keep)} legal of {len(new['cards'])} cards, "
      f"protocol {new.get('protocolVersion')}")
