langs = ['French', 'German']
langcodes = ['fr', 'de']
messages = {}

message = {}
pending_key = None
pending_value = None

for lang in langs:
    data = open('C:/FightClub/data/messages/ClientMessages.%s.translation'%lang).read().decode('utf-8')
    if data.startswith(u'\ufeff'):
        # BOM, die in a fire please
        data = data[1:]

    for line in data.splitlines():
        line = line.strip()
        if line == 'Message' or line == '{' or not line:
            continue
        if line == '}':
            if message.get('Scope') == 'CrypticLauncher':
                messages.setdefault(message['DefaultString'], {})[lang] = message['TranslatedString']
            message = {}
            continue
        if pending_key is not None:
            pending_value += line
            key, value = pending_key, pending_value
            pending_key, pending_value = None, None
        else:
            key, value = line.split(' ', 1)
        if not value.endswith('&>'):
            pending_key = key
            pending_value = value + '\n'
            continue
        if value.startswith('<&') and value.endswith('&>'):
            value = value[2:-2]
        message[key] = value

outfilebase = 'C:/src/Core/CrypticLauncher/messages/%s.dat'
eng = open(outfilebase%'en', 'wb')
langfiles = {}
for lang, code in zip(langs, langcodes):
    langfiles[lang] = open(outfilebase%code, 'wb')
for key, values in messages.iteritems():
    eng.write(key.encode('utf-8') + '\0')
    for lang in langs:
        langfiles[lang].write(values[lang].encode('utf-8') + '\0')
        if u'<&' in values[lang]:
	    print values[lang].encode('utf-8')
eng.close()
for f in langfiles.itervalues():
    f.close()
