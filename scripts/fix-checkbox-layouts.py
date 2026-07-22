from pathlib import Path
import re

w = Path(r"C:\hoenn-forge\overlay\azahar\res\layout\activity_hoenn_welcome.xml")
t = w.read_text(encoding="utf-8")
# Fix accidental `n literals
t = t.replace(
    'style="@style/Hoenn.CheckBox"`n                    android:text="@string/hoenn_welcome_checkbox"`n                    android:textSize="14sp" />',
    '''style="@style/Hoenn.CheckBox"
                    android:text="@string/hoenn_welcome_checkbox"
                    android:textSize="14sp" />''',
)
t = t.replace("\\n", "\n")  # only if double-escaped - careful
# safer: only replace the known broken pattern
if "`n" in t:
    t = t.replace("`n", "\n")
w.write_text(t, encoding="utf-8")
print("welcome written, has style:", "Hoenn.CheckBox" in t)

r = Path(r"C:\hoenn-forge\overlay\azahar\res\layout\activity_hoenn_randomizer.xml")
rt = r.read_text(encoding="utf-8")
rt2 = re.sub(
    r'android:button="@drawable/hoenn_checkbox"\s*',
    'style="@style/Hoenn.CheckBox" ',
    rt,
)
rt2 = re.sub(r'android:paddingStart="10dp"\s*', "", rt2)
# remove redundant textColor on checkboxes only when style present nearby - keep simple
rt2 = re.sub(
    r'(style="@style/Hoenn\.CheckBox"[^>]*)\s*android:textColor="@color/hoenn_text"\s*',
    r"\1 ",
    rt2,
)
rt2 = re.sub(
    r'style="@style/Hoenn\.CheckBox"\s+style="@style/Hoenn\.CheckBox"',
    'style="@style/Hoenn.CheckBox"',
    rt2,
)
r.write_text(rt2, encoding="utf-8")
print("randomizer Hoenn.CheckBox count", rt2.count("Hoenn.CheckBox"))
print("remaining button drawable", rt2.count('android:button="@drawable/hoenn_checkbox"'))
