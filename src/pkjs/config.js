// Module ids must match the MOD_* enum in main.c.
var moduleOptions = [
  { label: 'None', value: '0' },
  { label: 'Day of Week', value: '1' },
  { label: 'Date', value: '2' },
  { label: 'Battery', value: '3' },
  { label: 'Bluetooth', value: '4' },
  { label: 'Weather', value: '5' },
  { label: 'Steps', value: '6' },
  { label: 'Distance', value: '7' },
  { label: 'Calories', value: '8' }
];

function sideSection(title, key, defaults) {
  var items = [{ type: 'heading', defaultValue: title, size: 4 }];
  for (var i = 0; i < 5; i++) {
    items.push({
      type: 'select',
      messageKey: key + '[' + i + ']',
      label: 'Line ' + (i + 1),
      defaultValue: defaults[i],
      options: moduleOptions
    });
  }
  return { type: 'section', items: items };
}

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Bill Cipher Settings'
  },
  {
    type: 'section',
    items: [
      {
        type: 'select',
        messageKey: 'TIME_COLOR',
        label: 'Time Color',
        defaultValue: 'anaglyph',
        options: [
          { label: 'Anaglyph', value: 'anaglyph' },
          { label: 'White', value: 'white' }
        ],
        capabilities: ['COLOR']
      },
      {
        type: 'select',
        messageKey: 'UNITS',
        label: 'Temperature Units',
        defaultValue: '0',
        options: [
          { label: 'Celsius', value: '0' },
          { label: 'Fahrenheit', value: '1' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'FLICK_ANIMATION',
        label: 'Animation on flick',
        description: 'If off, the hover animation only plays when the watchface loads.',
        defaultValue: true
      }
    ]
  },
  sideSection('Left Side', 'LEFT_MODULE', ['1', '2', '4', '5', '0']),
  sideSection('Right Side', 'RIGHT_MODULE', ['3', '6', '7', '8', '0']),
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
