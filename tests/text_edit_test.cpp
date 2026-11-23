#include "text_edit.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        TextEdit edit;
        std::string text = "abcd";
        auto key = [&](SDL_Scancode code, SDL_Keymod modifiers = SDL_KMOD_NONE)
        {
            SDL_KeyboardEvent event{};
            event.scancode = code;
            event.mod = modifiers;
            edit.key(text, event);
        };
        edit.begin(text);
        key(SDL_SCANCODE_LEFT);
        key(SDL_SCANCODE_LEFT);
        edit.insert(text, "XY");
        require(text == "abXYcd" && edit.position() == 4, "Typing did not insert at the caret");
        key(SDL_SCANCODE_DELETE);
        require(text == "abXYd", "Delete did not remove the next character");
        key(SDL_SCANCODE_BACKSPACE);
        require(text == "abXd", "Backspace did not remove the previous character");
        key(SDL_SCANCODE_HOME);
        key(SDL_SCANCODE_BACKSPACE);
        require(edit.position() == 0 && text == "abXd", "Backspace moved before the beginning");
        key(SDL_SCANCODE_END);
        key(SDL_SCANCODE_DELETE);
        require(edit.position() == text.size() && text == "abXd", "Delete moved past the end");
        key(SDL_SCANCODE_LEFT, SDL_KMOD_SHIFT);
        key(SDL_SCANCODE_LEFT, SDL_KMOD_SHIFT);
        require(edit.selection() == std::pair<std::size_t, std::size_t>{2, 4}, "Shift did not extend selection");
        edit.insert(text, "!");
        require(text == "ab!" && edit.position() == 3, "Typing did not replace selection");
        key(SDL_SCANCODE_A, SDL_KMOD_CTRL);
        require(text == "ab!" && edit.selection() == std::pair<std::size_t, std::size_t>{0, 3},
                "Ctrl+A must select text without clearing it");
        key(SDL_SCANCODE_C, SDL_KMOD_CTRL);
        auto* clipboard = SDL_GetClipboardText();
        require(clipboard && std::string(clipboard) == text, "Copy did not use the selected text");
        SDL_free(clipboard);
        key(SDL_SCANCODE_X, SDL_KMOD_CTRL);
        require(text.empty() && edit.position() == 0, "Cut did not remove the selection");
        key(SDL_SCANCODE_V, SDL_KMOD_CTRL);
        require(text == "ab!", "Paste did not insert the clipboard");
        key(SDL_SCANCODE_HOME);
        require(SDL_SetClipboardText("first\nsecond\tthird"), SDL_GetError());
        key(SDL_SCANCODE_V, SDL_KMOD_CTRL);
        require(text == "first second thirdab!", "Clipboard paste broke the single-line field");

        text = "é🙂界";
        edit.begin(text);
        key(SDL_SCANCODE_LEFT);
        require(edit.position() == 6, "Left split a UTF-8 character");
        key(SDL_SCANCODE_BACKSPACE);
        require(text == "é界" && edit.position() == 2, "Backspace split a UTF-8 character");
        key(SDL_SCANCODE_DELETE);
        require(text == "é", "Delete split a UTF-8 character");
        key(SDL_SCANCODE_LEFT);
        key(SDL_SCANCODE_RIGHT, SDL_KMOD_SHIFT);
        edit.insert(text, "ñ");
        require(text == "ñ" && edit.position() == 2, "Unicode selection replacement failed");

        text = "one two  three";
        edit.begin(text);
        key(SDL_SCANCODE_LEFT, SDL_KMOD_CTRL);
        require(edit.position() == 9, "Ctrl+Left did not move to the previous word");
        key(SDL_SCANCODE_BACKSPACE, SDL_KMOD_CTRL);
        require(text == "one three" && edit.position() == 4, "Ctrl+Backspace did not delete the previous word");
        key(SDL_SCANCODE_DELETE, SDL_KMOD_CTRL);
        require(text == "one ", "Ctrl+Delete did not delete the next word");
        text = "one two";
        edit.begin(text);
        key(SDL_SCANCODE_HOME);
        key(SDL_SCANCODE_RIGHT, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
        require(edit.selection() == std::pair<std::size_t, std::size_t>{0, 4}, "Word selection failed");
        key(SDL_SCANCODE_RIGHT);
        require(edit.position() == 4 && edit.selection().first == edit.selection().second,
                "Right did not collapse selection to its end");
        edit.place(1);
        edit.place(3, true);
        key(SDL_SCANCODE_DELETE);
        require(text == "o two", "Mouse-style placement did not preserve the selection anchor");

        text.assign(511, 'a');
        edit.begin(text);
        edit.insert(text, "é");
        require(text.size() == 511 && edit.position() == 511, "Length limit split a UTF-8 insertion");
        edit.insert(text, "ab");
        require(text.size() == 512 && text.back() == 'a', "Length limit was not enforced");
        key(SDL_SCANCODE_A, SDL_KMOD_CTRL);
        edit.insert(text, "界");
        require(text == "界", "Selection replacement at the length limit failed");
        key(SDL_SCANCODE_A, SDL_KMOD_CTRL);
        edit.insert(text, "");
        require(text == "界", "Empty paste deleted the selection");
        key(SDL_SCANCODE_BACKSPACE);
        require(text.empty(), "Backspace did not delete selected Unicode text");
        SDL_Quit();
        std::cout << "Text cursor, Unicode, selection, word editing and clipboard passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
