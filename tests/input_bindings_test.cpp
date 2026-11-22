#include "frontend.h"
#include "ui_text.h"
#include "virtual_gamepad.h"
#include <libretro.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static size_t row(const InputBindings& bindings, const std::string& name) {
    for (size_t i=0;i<bindings.rows().size();++i) if (bindings.rows()[i].name == name) return i;
    throw std::runtime_error("Missing action: " + name);
}
static void key(Frontend& ui, SDL_Scancode code) {
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=code; ui.event(e);
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected scratch directory");
        auto dir=std::filesystem::path(argv[1]) / ("input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        isolate_virtual_gamepads();
        require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD),SDL_GetError());
        SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
        identify_virtual_gamepad(desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1; desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1; desc.name="Cabinet input test";
        auto id=SDL_AttachVirtualJoystick(&desc); require(id!=0,SDL_GetError());
        auto* joystick=SDL_OpenJoystick(id); auto* pad=SDL_OpenGamepad(id); require(joystick && pad,SDL_GetError());
        std::array<bool,SDL_SCANCODE_COUNT> keys{};
        auto axis=[&](SDL_GamepadAxis a,int value) { require(SDL_SetJoystickVirtualAxis(joystick,a,a>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER && value==0 ? -32768 : value),SDL_GetError()); SDL_UpdateJoysticks(); SDL_UpdateGamepads(); };
        auto button=[&](SDL_GamepadButton b,bool value) { require(SDL_SetJoystickVirtualButton(joystick,b,value),SDL_GetError()); SDL_UpdateJoysticks(); SDL_UpdateGamepads(); };
        for (const auto system : {"generic", "3ds", "dc", "gb", "gba", "gc", "gg", "nes", "n64", "nds", "md", "sms", "saturn", "psp", "psx", "snes", "pce"}) {
            InputBindings defaults; defaults.configure(system, {});
            const auto clicks = defaults.sample(nullptr, keys.data(), SDL_BUTTON_LMASK | SDL_BUTTON_RMASK);
            for (auto pressed : clicks.buttons) require(!pressed, "Non-Wii defaults mapped a mouse click to a game button");
        }
        for (int controller = 0; controller < 4; ++controller) {
            InputBindings wii; wii.configure("wii", {}, controller);
            const auto left = wii.sample(nullptr, keys.data(), SDL_BUTTON_LMASK);
            const auto right = wii.sample(nullptr, keys.data(), SDL_BUTTON_RMASK);
            require(left.buttons[wii.rows()[row(wii, "A")].target] && right.buttons[wii.rows()[row(wii, "B")].target],
                "Wii controller defaults lost mouse A/B bindings");
        }
        InputBindings input; input.configure("n64",dir);
        {
            InputBindings hotkeys; hotkeys.configure_hotkeys("n64", dir);
            require(hotkeys.rows().size() == 3 && hotkeys.rows()[0].key == SDL_SCANCODE_F5 &&
                hotkeys.rows()[1].key == SDL_SCANCODE_F7 && hotkeys.rows()[2].key == SDL_SCANCODE_TAB,
                "Keyboard hotkey defaults incorrect");
            for (const auto& binding : hotkeys.rows()) require(!binding.pad, "Gamepad hotkeys should default to unbound");
            hotkeys.add(0, false, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER + 1);
            hotkeys.add(0, true, SDL_SCANCODE_F6);
            InputBindings restored; restored.configure_hotkeys("n64", dir);
            require(restored.rows()[0].pad == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER + 1 &&
                restored.rows()[0].key == SDL_SCANCODE_F6 && restored.rows()[0].extra_keys == std::vector<int>{SDL_SCANCODE_F5}, "Mixed hotkey bindings did not persist");
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
            require(restored.sample(pad, keys.data()).buttons[0], "Gamepad hotkey was not sampled");
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
            restored.copy_to(dir / "game-input"); restored.assign(0, false, 0);
            hotkeys.configure_hotkeys("n64", dir);
            require(hotkeys.rows()[0].pad, "Game hotkey edits changed system settings");
        }
        {
            Frontend ui;
            ui.hotkeys.configure_hotkeys("n64", dir / "capture-hotkeys");
            require(ui.discover(), "Hotkey controller discovery failed"); ui.update(true, 0); ui.sample_gameplay(true);
            ui.page = Page::Controls; ui.selected = 2; key(ui, SDL_SCANCODE_RETURN); ui.update(true, 1); ui.sample_gameplay(true);
            key(ui, SDL_SCANCODE_RETURN); ui.update(true, 2); ui.sample_gameplay(true);
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true); ui.update(true, 3); ui.sample_gameplay(true);
            require(ui.hotkeys.rows()[0].pad == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER + 1 &&
                ui.hotkeys.rows()[0].key == SDL_SCANCODE_F5, "Hotkey capture did not accept gamepad alongside keyboard");
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false); ui.update(true, 4); ui.sample_gameplay(true);
            ui.can_save = ui.has_save = true; ui.confirm_save_states = false; ui.resume_with_message("test"); ui.update(true, 5); ui.sample_gameplay(true);
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true); ui.update(true, 6); ui.sample_gameplay(true);
            require(ui.take_action() == Action::Save, "Gamepad save hotkey did not fire");
            ui.update(true, 7); ui.sample_gameplay(true); require(ui.take_action() == Action::None, "Held gamepad save hotkey repeated");
            button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false); ui.update(true, 8); ui.sample_gameplay(true);
            ui.hotkeys.add(2, false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER + 1);
            button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true); ui.update(true, 9); ui.sample_gameplay(true);
            require(ui.fast_forward_held, "Gamepad fast-forward did not activate");
            button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false); ui.update(true, 10); ui.sample_gameplay(true);
            require(!ui.fast_forward_held, "Gamepad fast-forward did not stop on release");
        }
        axis(SDL_GAMEPAD_AXIS_LEFTX,24000); axis(SDL_GAMEPAD_AXIS_RIGHTY,-32768); axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);
        button(SDL_GAMEPAD_BUTTON_SOUTH,true); button(SDL_GAMEPAD_BUTTON_WEST,true); button(SDL_GAMEPAD_BUTTON_START,true);
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true); button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
        auto value=input.sample(pad,keys.data());
        require(value.axes[0]>16000 && value.axes[0]<32767 && value.axes[3]==-32767,"N64 analog stick/C input missing");
        require(value.buttons[0] && value.buttons[1] && value.buttons[3] && value.buttons[10] && value.buttons[11] && value.buttons[12],"N64 A/B/Start/L/R/Z incomplete");
        require(!value.buttons[7],"N64 left stick leaked into D-pad");
        require(value.state(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X)==value.axes[0],"Analog API did not forward magnitude");
        require(value.state(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_BUTTON,12)==32767,"Analog trigger pressure missing");
        require((static_cast<uint16_t>(value.state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK))&(1u<<12))!=0,"Bitmask missing Z");
        require(value.state(1,RETRO_DEVICE_ANALOG,0,0)==0 && value.state(0,RETRO_DEVICE_ANALOG,5,0)==0,"Invalid input query not zero");
        for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b) button(static_cast<SDL_GamepadButton>(b),false);
        for(int a=0;a<SDL_GAMEPAD_AXIS_COUNT;++a) axis(static_cast<SDL_GamepadAxis>(a),0);
        axis(SDL_GAMEPAD_AXIS_LEFTX,1000); require(input.sample(pad,keys.data()).axes[0]==0,"Stick drift escaped deadzone"); axis(SDL_GAMEPAD_AXIS_LEFTX,0);
        {
            InputBindings deadzones; deadzones.configure("gc", {});
            for (int percent : {0, 1, 5, 17, 30}) {
                const int cutoff = percent * 32767 / 100;
                for (auto stick : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_RIGHTX}) {
                    const int output = stick == SDL_GAMEPAD_AXIS_LEFTX ? 0 : 2;
                    for (int sign : {-1, 1}) {
                        axis(stick, sign * cutoff);
                        require(deadzones.sample(pad, keys.data(), 0, percent, 5).axes[output] == 0,
                                "Stick deadzone failed at its boundary");
                        axis(stick, sign * (cutoff + 2000));
                        require(deadzones.sample(pad, keys.data(), 0, percent, 5).axes[output] == sign * (2000 * 32767 / (32767 - cutoff)),
                                "Stick range was not rescaled outside its deadzone");
                        axis(stick, sign * 32767);
                        require(deadzones.sample(pad, keys.data(), 0, percent, 5).axes[output] == sign * 32767,
                                "Deadzone reduced maximum stick deflection");
                    }
                    axis(stick, 0);
                }
                for (auto trigger : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
                    const auto target = trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? RETRO_DEVICE_ID_JOYPAD_L2 : RETRO_DEVICE_ID_JOYPAD_R2;
                    for (int position : {cutoff, cutoff + 2000, 32767}) {
                        require(SDL_SetJoystickVirtualAxis(joystick, trigger, position * 2 - 32768), SDL_GetError());
                        SDL_UpdateJoysticks(); SDL_UpdateGamepads();
                        const int actual = SDL_GetGamepadAxis(pad, trigger);
                        const int expected = actual <= cutoff ? 0 : (actual - cutoff) * 32767 / (32767 - cutoff);
                        require(deadzones.sample(pad, keys.data(), 0, 5, percent).pressure[target] == expected,
                                "Trigger deadzone failed to rescale analog pressure");
                    }
                    axis(trigger, 0);
                }
            }
            keys[SDL_SCANCODE_A] = true;
            require(deadzones.sample(nullptr, keys.data(), 0, 30, 30).axes[0] == -32767,
                    "Controller deadzones must not attenuate keyboard input");
            keys.fill(false);
        }
        keys[SDL_SCANCODE_W]=true; keys[SDL_SCANCODE_L]=true;
        value=input.sample(pad,keys.data()); require(value.axes[1]==-32767 && value.axes[2]==32767,"N64 keyboard movement/C keys missing"); keys.fill(false);
        const auto a=row(input,"A"), b=row(input,"B"), z=row(input,"Z");
        input.assign(a,false,SDL_GAMEPAD_BUTTON_WEST+1);
        require(input.rows()[b].pad==SDL_GAMEPAD_BUTTON_SOUTH+1,"Conflicting binding was not swapped");
        input.assign(z,false,InputBindings::axis_source(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,true));
        input.assign(a,true,SDL_SCANCODE_T);
        InputBindings restored; restored.configure("n64",dir);
        require(restored.rows()[a].key==SDL_SCANCODE_T && restored.rows()[z].pad==input.rows()[z].pad,"Bindings did not persist");
        restored.configure("gba",dir); require(restored.rows()[row(restored,"A")].key==SDL_SCANCODE_X,"Profiles leaked between systems");
        restored.configure("n64",dir); restored.reset(); require(restored.rows()[a].pad==SDL_GAMEPAD_BUTTON_SOUTH+1,"Reset did not restore defaults");
        {
            InputBindings wii;
            for (int type = 0; type < 4; ++type) {
                wii.configure("wii", {}, type);
                auto has = [&](const char* name) { return std::any_of(wii.rows().begin(), wii.rows().end(), [&](const auto& b) { return b.name == name; }); };
                require(has("Waggle") == (type < 2), "Waggle shown for wrong Wii controller");
                if (type == 1) {
                    require(!has("C") && !has("Z"), "Sideways Remote exposes Nunchuck buttons");
                    for (const auto& b : wii.rows()) require(b.target < 16, "Sideways Remote exposes analog stick bindings");
                    require(has("1") && has("2") && has("Home"), "Sideways Remote buttons missing");
                }
                if (type == 0) require(has("C") && has("Z") && has("Nunchuck waggle"), "Nunchuck bindings missing");
                if (type == 2) require(has("ZL") && has("ZR") && !has("C"), "Classic Controller bindings incorrect");
                if (type == 3) {
                    InputBindings gc; gc.configure("gc", {});
                    require(wii.rows().size() == gc.rows().size(), "Wii GameCube bindings differ from GameCube profile");
                    for (size_t i=0;i<gc.rows().size();++i) require(wii.rows()[i].name == gc.rows()[i].name && wii.rows()[i].target == gc.rows()[i].target, "GameCube row mismatch");
                }
            }
        }
        {
            InputBindings profiles;
            profiles.configure("wii", dir / "wii-profiles", 0);
            profiles.assign(0, true, SDL_SCANCODE_T);
            profiles.select_wii_controller(1);
            require(profiles.rows()[0].key != SDL_SCANCODE_T, "Nunchuck mapping leaked into Sideways Remote");
            profiles.assign(0, true, SDL_SCANCODE_Y);
            profiles.select_wii_controller(0);
            require(profiles.rows()[0].key == SDL_SCANCODE_T, "Nunchuck remap lost when switching back");
            profiles.copy_to(dir / "wii-game");
            profiles.select_wii_controller(1);
            require(profiles.rows()[0].key == SDL_SCANCODE_Y, "Creating game config lost other controller profiles");
        }
        input.configure("gc",{}); axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,20000);
        value=input.sample(pad,keys.data()); require(value.pressure[12]>0 && value.pressure[12]<32767,"GameCube trigger lost analog range"); axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,0);
        for(const char* system : {"3ds","wii","dc","gb","gba","gc","gg","nes","n64","nds","md","sms","saturn","psp","psx","snes","pce","generic"}) {
            input.configure(system,{});
            for (const auto& binding : input.rows()) {
                require(binding.target < 24, "Separate digital stick alias is still exposed");
                if (std::string(system) == "nes" || std::string(system) == "gba")
                    require(binding.target < 16, "Digital system exposes analog actions");
            }
            for(const auto& binding:input.rows()) {
                if(binding.pad>=100) { auto ax=static_cast<SDL_GamepadAxis>((binding.pad-100)/2); axis(ax,(binding.pad&1)?32767:-32768); }
                else if(binding.pad) button(static_cast<SDL_GamepadButton>(binding.pad-1),true);
                value=input.sample(pad,keys.data());
                if(binding.target<16) require(value.buttons[binding.target],"System default does not activate its button");
                else if(binding.target<24) require(value.axes[(binding.target-16)/2]!=0,"System default does not activate analog direction");
                else require(value.buttons[binding.target-20],"Digital stick alias missing");
                for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i) button(static_cast<SDL_GamepadButton>(i),false);
                for(int i=0;i<SDL_GAMEPAD_AXIS_COUNT;++i) axis(static_cast<SDL_GamepadAxis>(i),0);
                for (int source : binding.extra_pads) {
                    auto ax = static_cast<SDL_GamepadAxis>((source - 100) / 2);
                    axis(ax, (source & 1) ? 32767 : -32768);
                    require(input.sample(pad, keys.data()).buttons[binding.target], "Merged stick direction does not activate D-pad");
                    axis(ax, 0);
                }
            }
        }
        input.configure("3ds",{});
        require(InputBindings::identify("game.3ds","custom.so") == "3ds" &&
            InputBindings::identify("game.elf","azahar_libretro.so") == "3ds", "Nintendo 3DS inference failed");
        require(input.rows()[row(input,"ZL")].target == RETRO_DEVICE_ID_JOYPAD_L2 &&
            input.rows()[row(input,"ZR")].target == RETRO_DEVICE_ID_JOYPAD_R2 &&
            input.rows()[row(input,"Circle Pad Left")].target == 16 &&
            input.rows()[row(input,"C-stick / Pointer Left")].target == 20, "Nintendo 3DS controls incomplete");
        input.configure("dc",{});
        require(input.rows().size() == 22, "Dreamcast Full Controller bindings are incomplete");
        require(input.rows()[row(input,"A")].target == RETRO_DEVICE_ID_JOYPAD_B &&
            input.rows()[row(input,"B")].target == RETRO_DEVICE_ID_JOYPAD_A &&
            input.rows()[row(input,"X")].target == RETRO_DEVICE_ID_JOYPAD_Y &&
            input.rows()[row(input,"Y")].target == RETRO_DEVICE_ID_JOYPAD_X, "Dreamcast face-button contract incorrect");
        require(input.rows()[row(input,"D")].key == SDL_SCANCODE_BACKSPACE, "Dreamcast D keyboard binding changed");
        axis(SDL_GAMEPAD_AXIS_RIGHTX,18000);
        require(input.sample(pad,keys.data()).axes[2] > 0, "Full Controller right analog stick not forwarded");
        axis(SDL_GAMEPAD_AXIS_RIGHTX,0);
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,20000); axis(SDL_GAMEPAD_AXIS_LEFTX,24000);
        value=input.sample(pad,keys.data());
        require(value.pressure[12]>0 && value.pressure[12]<32767 && value.axes[0]>0 && !value.buttons[7],
            "Dreamcast analog stick/trigger range incorrect");
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,0); axis(SDL_GAMEPAD_AXIS_LEFTX,0);
        require(InputBindings::identify("game.GDI","custom.so")=="dc", "Dreamcast extension inference failed");
        require(InputBindings::identify("game.cue","/cores/flycast_libretro.so")=="dc", "Dreamcast shared extension inference failed");
        require(InputBindings::identify("game.GBA","/cores/mgba_libretro.so")=="gba","GBA inference failed");
        require(InputBindings::identify("game.iso","/cores/ppsspp_libretro.so")=="psp","Ambiguous extension did not use core");
        require(InputBindings::identify("game.GG","/cores/picodrive_libretro.so")=="gg","Shared core lost system");
        {
            InputBindings multi; multi.configure("gba", dir / "multiple");
            const auto action = row(multi, "A");
            multi.add(action, true, SDL_SCANCODE_T);
            multi.add(action, true, SDL_SCANCODE_Y);
            multi.add(action, true, SDL_SCANCODE_T);
            keys.fill(false); keys[SDL_SCANCODE_T] = true;
            require(multi.sample(nullptr, keys.data()).buttons[RETRO_DEVICE_ID_JOYPAD_A], "First extra key failed");
            keys.fill(false); keys[SDL_SCANCODE_Y] = true;
            require(multi.sample(nullptr, keys.data()).buttons[RETRO_DEVICE_ID_JOYPAD_A], "Second extra key failed");
            keys[SDL_SCANCODE_T] = true;
            require(multi.sample(nullptr, keys.data()).pressure[RETRO_DEVICE_ID_JOYPAD_A] == 32767, "Multiple inputs stacked strength");
            keys.fill(false);
            multi.add(action, false, SDL_GAMEPAD_BUTTON_NORTH + 1);
            multi.add(action, false, SDL_GAMEPAD_BUTTON_WEST + 1);
            button(SDL_GAMEPAD_BUTTON_NORTH, true);
            require(multi.sample(pad, keys.data()).buttons[RETRO_DEVICE_ID_JOYPAD_A], "Extra gamepad button failed");
            button(SDL_GAMEPAD_BUTTON_NORTH, false); button(SDL_GAMEPAD_BUTTON_WEST, true);
            require(multi.sample(pad, keys.data()).buttons[RETRO_DEVICE_ID_JOYPAD_A], "Second gamepad button failed");
            button(SDL_GAMEPAD_BUTTON_WEST, false);
            InputBindings saved_multi; saved_multi.configure("gba", dir / "multiple");
            require(saved_multi.rows()[action].extra_keys == multi.rows()[action].extra_keys && saved_multi.rows()[action].extra_pads == multi.rows()[action].extra_pads, "Multiple bindings not persisted");
            saved_multi.add(action, true, 0);
            require(saved_multi.rows()[action].extra_keys.empty() && saved_multi.rows()[action].key == SDL_SCANCODE_UNKNOWN && !saved_multi.rows()[action].extra_pads.empty(), "Clearing keys changed gamepad bindings");
            Frontend ui; ui.bindings.configure("n64",dir); ui.discover(); ui.update(true,0); ui.sample_gameplay(true);
            ui.open_menu(); ui.page=Page::Controls; ui.selected=0; key(ui,SDL_SCANCODE_RETURN);
            require(ui.page==Page::Bindings,"Rebinding screen not opened");
            ui.selected=static_cast<int>(row(ui.bindings,"Z")); ui.update(true,16); ui.sample_gameplay(true); key(ui,SDL_SCANCODE_RETURN); ui.update(true,32); ui.sample_gameplay(true);
            axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767); ui.update(true,48); ui.sample_gameplay(true);
            require(ui.bindings.rows()[row(ui.bindings,"Z")].pad==InputBindings::axis_source(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,true),"Gamepad capture missed trigger");
            require(ui.input().state(0,RETRO_DEVICE_JOYPAD,0,12)==0,"Rebinding input leaked into game");
            ui.resume_with_message("test"); ui.update(true,64); ui.sample_gameplay(true); require(!ui.gameplay()[12],"Held remapped trigger leaked after resume");
            axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0); ui.update(true,80); ui.sample_gameplay(true);
            axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767); ui.update(true,96); ui.sample_gameplay(true); require(ui.gameplay()[12],"Remapped trigger did not reach gameplay");
            axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0); ui.update(true,112); ui.sample_gameplay(true);
            ui.open_menu(); ui.page=Page::Controls; ui.selected=1; key(ui,SDL_SCANCODE_RETURN); ui.update(true,128); ui.sample_gameplay(true);
            ui.selected=static_cast<int>(row(ui.bindings,"A")); key(ui,SDL_SCANCODE_RETURN); ui.update(true,144); ui.sample_gameplay(true); key(ui,SDL_SCANCODE_T);
            require(ui.bindings.rows()[row(ui.bindings,"A")].key==SDL_SCANCODE_T,"Keyboard capture failed");
            ui.update(true,160); ui.sample_gameplay(true); key(ui,SDL_SCANCODE_RETURN); ui.update(true,176); ui.sample_gameplay(true); key(ui,SDL_SCANCODE_ESCAPE);
            require(ui.page==Page::Bindings && ui.bindings.rows()[row(ui.bindings,"A")].key==SDL_SCANCODE_T,"Cancel changed binding");
            key(ui,SDL_SCANCODE_DELETE); require(ui.bindings.rows()[row(ui.bindings,"A")].key==SDL_SCANCODE_UNKNOWN,"Unbind failed");
            SDL_Window* window=SDL_CreateWindow("Bindings test",480,420,0); auto* renderer=SDL_CreateRenderer(window,nullptr);
            require(renderer!=nullptr,SDL_GetError());
            { TextRenderer text(renderer);
              ui.selected=static_cast<int>(ui.bindings.rows().size()-1);
              SDL_RenderClear(renderer); ui.draw(renderer,text,480,420);
              auto* surface=SDL_RenderReadPixels(renderer,nullptr); require(surface!=nullptr,SDL_GetError());
              require(SDL_SaveBMP(surface,(std::filesystem::path(argv[1])/"input-bindings.bmp").c_str()),SDL_GetError());
              SDL_DestroySurface(surface); require(SDL_RenderPresent(renderer),SDL_GetError());
            }
            ui.page=Page::Controls; ui.selected=6; key(ui,SDL_SCANCODE_RETURN);
            require(ui.page==Page::Confirm && ui.selected==0,"Restore defaults must default to cancel");
            key(ui,SDL_SCANCODE_ESCAPE);
            require(ui.page==Page::Controls && ui.bindings.rows()[row(ui.bindings,"A")].key==SDL_SCANCODE_UNKNOWN,"Cancel restored bindings");
            ui.selected=6; key(ui,SDL_SCANCODE_RETURN); ui.selected=1; key(ui,SDL_SCANCODE_RETURN);
            require(ui.page==Page::Controls && ui.bindings.rows()[row(ui.bindings,"A")].key==SDL_SCANCODE_X,"Confirmed defaults not restored");
            SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
            SDL_Event removed{}; removed.type=SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which=id;
            SDL_DetachVirtualJoystick(id); ui.event(removed); require(ui.paused(),"Disconnect during rebinding did not pause");
        }
        input.configure("gba",dir/"unwritable");
        std::ofstream(dir/"unwritable") << "not a directory";
        const auto original=input.rows()[row(input,"A")].key;
        bool failed=false; try { input.assign(row(input,"A"),true,SDL_SCANCODE_T); } catch(const std::exception&) { failed=true; }
        require(failed && input.rows()[row(input,"A")].key==original,"Failed save changed active mapping");
        std::ofstream(dir/"n64.conf") << "cabinet-input 1\n0 0 0\n";
        failed=false; try { input.configure("n64",dir); } catch(const std::exception&) { failed=true; }
        require(failed, "Obsolete input profile version was accepted");
        std::ofstream(dir/"n64.conf") << "cabinet-input 2\n0 99999 -1 0 0\n";
        failed=false; try { input.configure("n64",dir); } catch(const std::exception&) { failed=true; }
        require(failed && input.rows()[row(input,"A")].key==SDL_SCANCODE_X,"Corrupt profile did not retain safe defaults");
        SDL_CloseGamepad(pad); SDL_CloseJoystick(joystick); SDL_Quit();
        std::cout << "Per-system defaults, analog input, rebinding, persistence, capture and isolation passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; SDL_Quit(); return 1; }
}
