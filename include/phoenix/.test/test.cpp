#include <phoenix/phoenix.hpp>
using namespace nall;
using namespace phoenix;

struct MainWindow : Window {
  VerticalLayout layout;
  ListView listView;
  Button toggleFullScreen;
  Button quitButton;

  MainWindow() {
    setFrameGeometry({64, 64, 640, 480});

    layout.setMargin(5);
    listView.append(string{Desktop::size().width, ",", Desktop::size().height});
    for(unsigned n = 0; n < Monitor::count(); n++) listView.append(Monitor::geometry(n).text());
    listView.setSelection(1);
    layout.append(listView, {~0, ~0}, 5);
    toggleFullScreen.setText("Toggle Fullscreen");
    layout.append(toggleFullScreen, {~0, 0}, 5);
    quitButton.setText("Quit");
    layout.append(quitButton, {~0, 0});
    append(layout);

    onClose = &Application::quit;
    toggleFullScreen.onActivate = [&] { setFullScreen(!fullScreen()); };
    quitButton.onActivate = &Application::quit;

    listView.onActivate = [&] {
      if(modal() == false) {
        print("Base = ", listView.selection(), "\n");
        setModal(true);
      } else {
        print("Slot = ", listView.selection(), "\n");
        setModal(false);
        setVisible(false);
      }
    };

    setVisible();
    listView.setFocused();
  }
};

int main() {
  new MainWindow;
  Application::run();
  return 0;
}
