import re, shutil, os

base = r'K:\Project\Key\Plugins\sluaunreal\Plugins\slua_unreal\Source\slua_unreal'
inc = os.path.join(base, 'Private', 'LuaWrapper.inc')
head = os.path.join(base, 'Public', 'LuaWrapperHead.inc')
inc57 = os.path.join(base, 'Private', 'LuaWrapper5.7.inc')
head57 = os.path.join(base, 'Public', 'LuaWrapper5.7Head.inc')

with open(inc, 'r', encoding='utf-8') as f:
    content = f.read()

before = len(content)

# Remove getter blocks for CURRENT_FILE_ID
content = re.sub(
    r'\n        static int get_CURRENT_FILE_ID_\d+_GENERATED_BODY\(lua_State\* L\) \{[^}]+\}\n',
    '\n', content)

# Remove setter blocks
content = re.sub(
    r'\n        static int set_CURRENT_FILE_ID_\d+_GENERATED_BODY\(lua_State\* L\) \{[^}]+\}\n',
    '\n', content)

# Remove addField lines
content = re.sub(
    r'\n\s+LuaObject::addField\(L, "CURRENT_FILE_ID_\d+_GENERATED_BODY"[^\n]+\n',
    '\n', content)

after = len(content)

with open(inc57, 'w', encoding='utf-8') as f:
    f.write(content)

shutil.copy2(head, head57)

print(f'Post-processed: {before} -> {after} bytes ({before - after} removed)')
print(f'Deployed: {inc57}')
print(f'Deployed: {head57}')
