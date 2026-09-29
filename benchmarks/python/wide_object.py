class Large:
    def __init__(self):
        for index in range(32):
            setattr(self, f"champ{index:02}", index)


def principal():
    objet = Large()
    total = 0
    index = 0
    while index < 500_000:
        total = total + objet.champ00 + objet.champ31
        index = index + 1
    print(total)


principal()
