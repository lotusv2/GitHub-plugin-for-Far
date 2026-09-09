# GitHub plugin for Far Manager

Нативный DLL-плагин Far Manager для работы с репозиториями GitHub непосредственно из файловых панелей.

## Возможности v0.2.0

- настройка GitHub Fine-grained Personal Access Token через меню Far Manager;
- проверка токена запросом к GitHub API;
- шифрование токена средствами Windows DPAPI;
- автоматическая загрузка списка доступных репозиториев;
- навигация по каталогам репозитория;
- просмотр и редактирование файлов через встроенный редактор Far;
- сохранение изменений непосредственно в GitHub;
- копирование локальных файлов в GitHub;
- создание каталогов через `.gitkeep`;
- UTF-8 во всех исходных и текстовых файлах проекта.

## Настройка

1. Откройте Far Manager.
2. Вызовите меню плагинов `F11`.
3. Выберите `GitHub`.
4. Откройте конфигурацию плагина.
5. Введите GitHub Fine-grained Personal Access Token.
6. Плагин проверит токен и сохранит его в зашифрованном виде.

Для рекомендуемой конфигурации токена достаточно прав:

- **Metadata: Read-only**;
- **Contents: Read and write**.

Права для Pull Requests и Issues понадобятся только после реализации соответствующих функций.

## Сборка

Проект собирается в Visual Studio toolchain через CMake.

```text
cmake -S . -B build -A x64
cmake --build build --config Release
```

Для Win32:

```text
cmake -S . -B build32 -A Win32
cmake --build build32 --config Release
```

Результат:

```text
build/Release/FarGitHub.dll
```

Установите DLL в каталог:

```text
%FARHOME%\\Plugins\\FarGitHub\\FarGitHub.dll
```

## Архитектура

Подробное описание компонентов находится в `docs/ARCHITECTURE.md`.

## Дорожная карта

- **v0.1.0** — базовая виртуальная панель GitHub.
- **v0.2.0** — настройки, DPAPI и автоматический список репозиториев.
- **v0.3.0** — поиск и избранные репозитории.
- **v0.4.0** — выбор и переключение веток.
- **v0.5.0** — расширенные операции с файлами.
- **v1.0.0** — история, commits, pull requests и полноценный Git workflow.
