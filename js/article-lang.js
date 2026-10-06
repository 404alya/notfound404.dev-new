// Articles list: one card per translation group, in the visitor's language
// when the group has it. The build marks every card but the group's fallback
// (English, else the source) with data-hidden; this only moves the mark.
// Inlined right after the list, so it runs before the page fades in.
const visitorLang = (navigator.language || 'en').split('-')[0].toLowerCase();

const groups = new Map();
for (const card of document.querySelectorAll('#articles-box [data-group]')) {
  const cards = groups.get(card.dataset.group) || [];
  cards.push(card);
  groups.set(card.dataset.group, cards);
}

for (const cards of groups.values()) {
  const shown =
    cards.find(card => card.dataset.lang === visitorLang) ||
    cards.find(card => card.hasAttribute('data-fallback'));
  for (const card of cards) card.toggleAttribute('data-hidden', card !== shown);
}
