# GitHub-plugin-for-Far

Полноценный DLL-плагин для Far Manager для работы с GitHub как с виртуальной файловой системой.

## Что уже реализовано

- DLL-плагин с нативным Far Manager Plugin API.
- Панель GitHub.
- Навигация по каталогам репозитория.
- Открытие файлов в редакторе Far Manager.
- Отправка изменённого файла обратно в GitHub отдельным коммитом.
- Создание каталогов через `.gitkeep`.
- HTTPS через системный WinHTTP, без curl/libcurl.
- CMake-сборка для x64 и Win32.

## Настройка

Пока используется простой первый вариант конфигурации через переменные окружения:

```bat
setx FAR_GITHUB_TOKEN "github_pat_..."
setx FAR_GITHUB_REPOSITORY "lotusv2/GitHub-plugin-for-Far"
```

Для работы с приватным репозиторием токен должен иметь права, необходимые для чтения и записи содержимого репозитория.

## Запуск

После установки DLL в каталог `%FARHOME%\Plugins\FarGitHub\`:

`F11` → `GitHub`

## Архитектура

```text
src/
├── FarGitHub.cpp/.hpp       # точка сборки DLL
├── Plugin.cpp/.hpp          # Far Manager Plugin API
├── Panel.cpp/.hpp           # виртуальная панель GitHub
├── GitHubClient.cpp/.hpp    # REST API GitHub
├── FarGitHub.def             # экспорт DLL
└── resource.rc               # версия Windows DLL
```

## План развития

1. Выбор пользователя и репозитория прямо из Far.
2. Выбор ветки.
3. Полноценные Create/Rename/Delete.
4. Copy/Move файлов между Far и GitHub.
5. Commit message и выбор ветки перед записью.
6. Просмотр истории коммитов.
7. Diff перед отправкой изменений.
8. Настройки токена внутри Far Manager.
9. Кэширование и работа с большими репозиториями.

См. `docs/BUILD.md` для сборки.
