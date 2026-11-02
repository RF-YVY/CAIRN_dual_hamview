const confirmation = document.getElementById('confirm');
const flash = document.getElementById('flash');
const hint = document.getElementById('install-hint');
confirmation.addEventListener('change', () => {
  flash.disabled = !confirmation.checked;
  hint.textContent = confirmation.checked
    ? 'Ready. Connect the ESP32 CH340 port. The full image replaces existing ESP32 data.'
    : 'Confirm the hardware and fresh-install notice to enable installation.';
});
setTimeout(() => {
  if (!customElements.get('esp-web-install-button')) {
    document.getElementById('component-status').textContent =
      'The browser installer could not load. Check your connection or use the download and command-line instructions below.';
  }
}, 15000);
