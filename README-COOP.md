# Picori Co-op — локальный кооператив для Minish Cap

Мод для [Project Picori](https://github.com/999sian/tmc), PC-порта *The Legend of Zelda: The Minish Cap*. Добавляет второго Линка на том же компьютере: два игрока проходят всю игру вместе, на одном экране, с клавиатуры или геймпадов.

Версия: **v0.1.0** (порт 0.9.5). Номер виден в заголовке окна.

## Что умеет

- **Второй Линк в игре.** Он ходит и сражается сам, со своими предметами на A/B и своими сердцами (сердца можно сделать общими).
- **Камера** держит в кадре обоих. Линки не расходятся дальше одного экрана.
- **Враги и боссы** выбирают ближайшего Линка.
- **Действия Линка 2:** нажимает кнопки и плиты, носит и бросает горшки, толкает блоки. Открывает сундуки и двери, читает таблички, говорит с NPC, сливает кинстоуны.
- **Платформы, кувшинки, облака, вагонетки, бочка Deepwood, Gleerok и Gyorg** везут обоих.
- **Переходы, порталы и сюжетные сцены** ведёт Линк 1. Линк 2 на это время убирается и появляется рядом. Уменьшаются Линки только вместе.
- **Если Линк 2 потерял все сердца**, он выбывает на 3 секунды и возвращается к Линку 1. Конец игры наступает, только если сердца кончились у Линка 1.
- **Сохранение помнит Линка 2:** его предметы и сердца лежат в отдельном файле `tmc.coop`, сам `tmc.sav` не меняется. Быстрые сохранения тоже запоминают Линка 2.

## Сборка

Нужен свой дамп ROM *The Minish Cap* (USA). В репозитории ROM нет и не будет.

```bash
python3 build.py --usa
```

Готовая игра появится в `dist/USA/tmc_pc`. Подробности о зависимостях и других платформах — в [INSTALL.md](INSTALL.md).

## Управление

| Действие | Линк 1 (клавиатура) | Линк 2 (клавиатура) |
|---|---|---|
| Движение | стрелки | I / J / K / L |
| A (меч, действие) | X | O |
| B (предмет) | Z | U |
| R (действие, подъём, перекат) | S | H |
| L (кинстоуны) | A | Y |
| Start (меню) | Enter | N |

- **Геймпады.** Первый подключённый геймпад достаётся Линку 1, второй — Линку 2. Если отключить геймпад Линка 1, геймпад Линка 2 останется у Линка 2. Подключённый обратно геймпад снова достанется Линку 1.
- **Как подключить Линка 2:** нажми любую его клавишу или кнопку его геймпада. Если игра была сохранена вдвоём, при загрузке файла Линк 2 появляется сам.
- **Меню Линка 2** (клавиша N): там выбираются его предметы на A/B. Кнопки предметов Линка 2 и его сердца видны вверху экрана.
- **Текст** листают кнопки любого из Линков.

## Настройки (`dist/USA/config.json`)

| Ключ | По умолчанию | Что делает |
|---|---|---|
| `coop_p2_join` | `true` | Линк 2 подключается по нажатию его клавиш. `false` — не подключается случайно |
| `coop_shared_hearts` | `false` | `true` — одна общая полоса сердец, как в одиночной игре |
| `p2_gamepad` | `"auto"` | `"auto"`: второй геймпад у Линка 2; `"first"`: первый (единственный) геймпад у Линка 2, Линк 1 на клавиатуре; `"none"`: Линк 2 только на клавиатуре |
| `p2_bindings` | см. таблицу выше | клавиши Линка 2 |

## Режим «кооп-тест»

Позволяет сразу попасть в нужную комнату и проверить механики: враги, кнопки, порталы, боссы. Комнаты выбираются кнопками на стартовом экране под **Play** или в меню **F8 → Coop Test**. Из терминала то же самое:

```bash
cd dist/USA
TMC_COOP_TEST=enemies ./tmc_pc
```

Пресеты описаны в `assets/coop_tests.json`.

## Известные ограничения

- Магазины, слияние кинстоунов с сюжетом NPC, порталы миниша, вагонетки и облака ведёт Линк 1. Линк 2 едет пассажиром или ждёт рядом.
- На время мини-игр (куры Anju, сундуки Борлова) Линк 2 убирается.
- NPC в разговоре поворачивается к Линку 1, даже если заговорил Линк 2.
- Дешёвые клавиатуры теряют часть одновременных нажатий двух игроков. Удобнее играть с геймпадами.
- Быстрые сохранения прежних версий порта этой версией не загружаются. Обычные сохранения (`tmc.sav`) работают.

## Благодарности и лицензия

- [999sian/tmc — Project Picori](https://github.com/999sian/tmc): PC-порт, на котором построен мод.
- [zeldaret/tmc](https://github.com/zeldaret/tmc): декомпиляция *The Minish Cap*.

Код распространяется под той же лицензией, что и порт: GPL-3.0, см. [LICENSE](LICENSE). *The Legend of Zelda* и *The Minish Cap* принадлежат Nintendo. Мод не содержит ROM и данных из него.

---

## English

**Picori Co-op** adds local two-player co-op to [Project Picori](https://github.com/999sian/tmc), a PC port of *The Legend of Zelda: The Minish Cap*. A second Link plays the whole game alongside the first on one screen. Each Link has his own A/B items and hearts (hearts can be shared). The camera keeps both Links in view. Player 1 drives room transitions, cutscenes, shops and portals.

- **Build:** bring your own USA ROM, run `python3 build.py --usa`, then start `dist/USA/tmc_pc`.
- **Player 1 keys:** arrows, X = A, Z = B, S = R, A = L, Enter = Start.
- **Player 2 keys:** I/J/K/L, O = A, U = B, H = R, Y = L, N = Start (opens player 2's item menu).
- **Gamepads:** the second pad belongs to player 2. Pads keep their player when one is unplugged.
- **Joining:** player 2 joins by pressing any of its keys. A save made with two players brings player 2 back on load.
- **Config (`config.json`):** `coop_p2_join`, `coop_shared_hearts`, `p2_gamepad` (`auto` / `first` / `none`), `p2_bindings`.
- **Test mode:** `TMC_COOP_TEST=<preset> ./tmc_pc`, or use the buttons on the launch screen.

Licensed under GPL-3.0 like the port it builds on. Credits: 999sian/tmc (Project Picori), zeldaret/tmc. No ROM or ROM-derived data is included.
