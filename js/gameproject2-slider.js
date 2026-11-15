// gameproject2 slider: one slide (image or video) is shown at a time, marked
// with data-active. The arrows, and the left/right keys while focus is inside
// the slider, step through the slides and wrap around at both ends. A video
// plays (looping) only while its slide is shown and the slider is on screen.
const slider = document.querySelector('[data-gameproject2-slider]');

if (slider) {
  const slides = [...slider.querySelectorAll('[data-gameproject2-slide]')];
  const status = slider.querySelector('[data-gameproject2-status]');
  let current = Math.max(0, slides.findIndex(slide => slide.hasAttribute('data-active')));

  const isVideo = slide => slide instanceof HTMLVideoElement;
  let inView = false;

  // Play the shown slide's video while the slider is on screen; pause the rest.
  function syncVideos() {
    slides.forEach((slide, n) => {
      if (!isVideo(slide)) return;
      if (inView && n === current) slide.play().catch(() => {});
      else slide.pause();
    });
  }

  function show(index) {
    current = (index + slides.length) % slides.length;
    slides.forEach((slide, n) => slide.toggleAttribute('data-active', n === current));
    syncVideos();
    if (status) status.textContent = `Slide ${current + 1} of ${slides.length}`;
  }

  new IntersectionObserver(entries => {
    inView = entries[entries.length - 1].isIntersecting;
    syncVideos();
  }, { threshold: 0.25 }).observe(slider);

  slider.querySelectorAll('[data-gameproject2-prev], [data-gameproject2-next]').forEach(button => {
    const step = button.hasAttribute('data-gameproject2-next') ? 1 : -1;
    button.hidden = false;
    button.addEventListener('click', () => show(current + step));
  });

  slider.addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft') show(current - 1);
    else if (event.key === 'ArrowRight') show(current + 1);
    else return;
    event.preventDefault();
  });
}
