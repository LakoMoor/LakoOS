(function(){
  var root=document.documentElement;
  var saved=localStorage.getItem('theme');
  var dark=saved?saved==='dark':window.matchMedia('(prefers-color-scheme: dark)').matches;
  root.setAttribute('data-theme',dark?'dark':'light');
  var tb=document.getElementById('theme');
  function paintTheme(){tb.textContent=root.getAttribute('data-theme')==='dark'?'☀':'☾'}
  paintTheme();
  tb.addEventListener('click',function(){
    var n=root.getAttribute('data-theme')==='dark'?'light':'dark';
    root.setAttribute('data-theme',n);localStorage.setItem('theme',n);paintTheme();
  });

  var I18N={
    ru:{
      'nav.home':'Главная','nav.features':'Возможности','nav.docs':'Документация','nav.compat':'Совместимость','nav.about':'О проекте','nav.desktop':'Рабочий стол','nav.names':'Структура','nav.download':'Загрузка','nav.faq':'Вопросы','nav.get':'Скачать',
      'hero.pill':'Предварительная версия','hero.t1':'Свободная NT.','hero.t2':'С привычным интерфейсом.',
      'hero.lead':'LakoOS — свободная операционная система на архитектуре NT: меню «Пуск» по центру, привычный Проводник, те же папки и те же имена программ.',
      'hero.dl':'Скачать LakoOS','hero.gh':'Открыть на GitHub','hero.fine':'64-бит (amd64) · QEMU, VirtualBox, Hyper-V · свободное ПО под GNU GPL',
      'f.eyebrow':'Что внутри','f.title':'Всё на своих местах','f.sub':'Знакомая оболочка, знакомые программы и привычная структура диска.',
      'd.eyebrow':'Рабочий стол','d.title':'Посмотрите, как это выглядит','d.sub':'Переключайте экраны: меню «Пуск», Проводник, Панель управления и командная строка.',
      'n.eyebrow':'Структура','n.title':'Те же папки. Те же имена.','n.sub':'Каталоги и исполняемые файлы называются как в Windows 11, поэтому скрипты, ярлыки и привычки работают.',
      'dl.eyebrow':'Загрузка','dl.title':'Выберите, как попробовать','dl.sub':'Всё бесплатно. Рекомендуем виртуальную машину.',
      'rq.title':'Системные требования','faq.title':'Частые вопросы',
      'band.t':'Попробуйте LakoOS сегодня','band.p':'Скачайте образ, запустите в виртуальной машине и расскажите, что получилось.'
    }
  };
  var en={};
  document.querySelectorAll('[data-i18n]').forEach(function(el){en[el.getAttribute('data-i18n')]=el.innerHTML});
  var lb=document.getElementById('lang');
  function setLang(l){
    document.querySelectorAll('[data-i18n]').forEach(function(el){
      var k=el.getAttribute('data-i18n');
      el.innerHTML=(l==='ru'&&I18N.ru[k])?I18N.ru[k]:en[k];
    });
    document.documentElement.lang=l;lb.textContent=l==='ru'?'EN':'RU';localStorage.setItem('lang',l);
  }
  lb.addEventListener('click',function(){setLang(document.documentElement.lang==='ru'?'en':'ru')});
  var sl=localStorage.getItem('lang')||((navigator.language||'').indexOf('ru')===0?'ru':'en');
  if(sl==='ru')setLang('ru');

  var tabs=document.querySelectorAll('.tab');
  var img=document.getElementById('tabimg'),h=document.getElementById('tabh'),p=document.getElementById('tabp'),ul=document.getElementById('tabul');
  var DATA={
    start11:{img:'assets/screens/start-win11.png',h:'Start, centered',p:'Pinned apps, recent items and search in one rounded panel, right above the taskbar.',l:['Pinned grid and Recommended list','Instant search while you type','Power menu: Lock, Sign out, Shut down, Restart']},
    start10:{img:'assets/screens/start-win10.png',h:'Start, list and tiles',p:'Prefer the classic layout? One setting switches to the left-aligned taskbar, an A–Z list and tiles.',l:['Alphabetical app list with letters','Tiles for pinned apps','Navigation rail on the left']},
    explorer:{img:'assets/screens/explorer.png',h:'File Explorer',p:'A ribbon, Quick access, This PC and drives with free-space bars, in a clean dark window.',l:['It is explorer.exe, in the Windows folder','Navigation pane and breadcrumb bar','Search box and view switcher']},
    control:{img:'assets/screens/control.png',h:'Control Panel',p:'Categories, search and quick tasks. control.exe still opens any .cpl you pass to it.',l:['Category view with common tasks','Same applets behind the scenes','Dark and light themes']},
    cmd:{img:'assets/screens/cmd.png',h:'Command Prompt',p:'The command line you know, with the usual tools in System32.',l:['cmd.exe, ipconfig, ping, reg, sc, tasklist','Console host with the standard colors','Runs your existing batch files']}
  };
  function pick(k){
    if(!img)return;
    var d=DATA[k];img.src=d.img;h.textContent=d.h;p.textContent=d.p;
    ul.innerHTML=d.l.map(function(x){return '<li>'+x+'</li>'}).join('');
    tabs.forEach(function(t){t.setAttribute('aria-selected',t.dataset.k===k?'true':'false')});
  }
  tabs.forEach(function(t){t.addEventListener('click',function(){pick(t.dataset.k)})});
  if(img)pick('start11');

  document.querySelectorAll('.copy').forEach(function(b){
    b.addEventListener('click',function(){
      var t=b.parentElement.querySelector('pre').innerText;
      (navigator.clipboard?navigator.clipboard.writeText(t):Promise.reject()).then(function(){
        var o=b.textContent;b.textContent='Copied';setTimeout(function(){b.textContent=o},1400);
      }).catch(function(){});
    });
  });

  var win=/Windows/.test(navigator.userAgent);
  var bs=document.getElementById('buildcmd');
  if(bs&&win){bs.textContent='git clone https://github.com/LakoMoor/LakoOS.git\ncd LakoOS\n.\\buildenv\\build.ps1'}

  var io=new IntersectionObserver(function(es){es.forEach(function(e){if(e.isIntersecting){e.target.classList.add('in');io.unobserve(e.target)}})},{threshold:.12});
  document.querySelectorAll('.reveal').forEach(function(el){io.observe(el)});
})();
