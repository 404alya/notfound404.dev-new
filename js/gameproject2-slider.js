// gameproject2 slider: one slide (image or video) is shown at a time, marked
// with data-active. The arrows, and the left/right keys while focus is inside
// the slider, step through the slides and wrap around at both ends. A video
// is paused while its slide is hidden and resumes when it's shown again.
const slider = document.querySelector('[data-gameproject2-slider]');

if (slider) {
  const slides = [...slider.querySelectorAll('[data-gameproject2-slide]')];
  const status = slider.querySelector('[data-gameproject2-status]');
  let current = Math.max(0, slides.findIndex(slide => slide.hasAttribute('data-active')));

  const isVideo = slide => slide instanceof HTMLVideoElement;

  function show(index) {
    current = (index + slides.length) % slides.length;
    slides.forEach((slide, n) => {
      const active = n === current;
      slide.toggleAttribute('data-active', active);
      if (!isVideo(slide)) return;
      if (active) slide.play().catch(() => {});
      else slide.pause();
    });
    if (status) status.textContent = `Slide ${current + 1} of ${slides.length}`;
  }

  // Hidden videos carry autoplay like every video on the site; pausing them
  // also clears their autoplay flag, so they stay paused until shown.
  slides.forEach((slide, n) => {
    if (n !== current && isVideo(slide)) slide.pause();
  });

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
